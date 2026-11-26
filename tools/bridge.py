#!/usr/bin/env python3
"""Prismatic host bridge: the Flipper's own RPC over USB, and the app's files on the SD card.

The FAP has no host interface of its own; its contract with a computer is the files under
SD/apps_data/prismatic/ (docs/bridge.md). This program reads and writes them over the official
RPC protocol (flipperzero-protobuf: storage, system, app lock status), and decodes what the
reader saved against a pack.

  tools/bridge.py status [--pack P]           device, card space, the app's files, pack current?
  tools/bridge.py push-pack [P] [--force]     install a pack: temporary name, MD5 check, rename
  tools/bridge.py pull OUT_DIR                copy sessions.jsonl and progress off the card
  tools/bridge.py sessions FILE [--pack P]    saved detector sessions as JSON, markers named
  tools/bridge.py progress FILE [FILE2] [--pack P]   quiz progress (Leitner boxes) as JSON

It writes only *.pkp files in the app's folder, refuses while an application holds the device,
and never deletes what the reader saved. The device commands take --port (default: the first
Flipper found); sessions and progress work offline on pulled files.
"""

import argparse
import hashlib
import json
import re
import struct
import sys
import time
import zlib
from pathlib import Path

import flipper_screen as fs
import pocket

ROOT = Path(__file__).resolve().parent.parent
APP_DIR = "/ext/apps_data/prismatic"
FAP_PATH = "/ext/apps/Tools/prismatic.fap"
DEFAULT_PACK = ROOT / "build" / "packs" / "kompendium.pkp"
PULLED = ("sessions.jsonl", "progress.bin", "progress.tmp")
# A pack goes over the storage CLI in acknowledged chunks (`storage write_chunk`, as uFBT's
# storage.py sends files): the device answers each chunk before the next is sent, so the host
# never runs ahead of the card and never ends a session inside a long write.
CHUNK = 8192
CHUNK_TIMEOUT = 60.0
# The device hashes a 20 MB pack in about 50 s, and its `storage rename` copies the file (470 KB
# took 3.5 to 5 s, so 20 MB takes minutes); a long storage command waits this long.
MD5_TIMEOUT = 600.0

# PB.Main content fields (flipper.proto) and their payload fields (storage.proto, system.proto).
PING_REQ, LIST_REQ, LIST_RESP, READ_REQ, READ_RESP = 5, 7, 8, 9, 10
MD5_REQ, MD5_RESP = 14, 15
LOCK_REQ, LOCK_RESP, STAT_REQ, STAT_RESP = 17, 18, 24, 25
INFO_REQ, INFO_RESP, DEVINFO_REQ, DEVINFO_RESP = 28, 29, 32, 33
OK, STORAGE_NOT_EXIST = 0, 7
FILE_TYPE, FILE_NAME, FILE_SIZE, FILE_DATA = 1, 2, 3, 4

MARKS = {"O": "observed", "N": "not_observed", "U": "unknown", "A": "not_applicable",
         "-": "unanswered"}
PROGRESS_HEADER, PROGRESS_ENTRY, PROGRESS_CAP, PROGRESS_VERSION = 20, 12, 256, 1


class BridgeError(Exception):
    pass


def fields(buf):
    """Fields of one message as {number: [values]}; repeated fields keep their order."""
    out = {}
    for num, _wire, val in fs.parse_fields(buf):
        out.setdefault(num, []).append(val)
    return out


def one(msg, num, default=None):
    return msg.get(num, [default])[0]


def text(num, value):
    return fs.field(num, 2, value.encode("utf-8"))


class Device:
    """One RPC session: every request waits for its own responses, has_next included."""

    def __init__(self, port, tries=5):
        self.rpc = fs.Rpc(port)
        self.pending = 0  # the command id still waiting for its answer
        # The device can drop the first request after start_rpc_session (measured on 1.5.1: an
        # MD5 request sent first went unanswered for ten minutes). Ping until one is answered.
        for _ in range(tries):
            try:
                self.request(PING_REQ, timeout=1.0)
                return
            except TimeoutError:
                continue
        raise BridgeError("the device opened an RPC session but answers no ping")

    def close(self, grace=MD5_TIMEOUT):
        """StopSession, back to the CLI; never while a request is unanswered.

        Ending the session inside a long storage request (an MD5 given up after 120 s, a write
        whose status was not awaited) took the Flipper off USB twice, and releasing the port
        ends it as well. So a request still pending is waited for (up to `grace` seconds more)
        before the session ends; only if it never answers is the port released with a warning.
        """
        if self.pending:
            try:
                for _raw in self._responses(self.pending, grace):
                    pass
                self.pending = 0
            except (TimeoutError, ValueError, IndexError):
                print("bridge: the device never answered its last request; it may need a "
                      "restart (hold Left and Back)", file=sys.stderr)
        if not self.pending:
            self.stop_session()
        self.rpc.s.close()

    def stop_session(self, timeout=3.0):
        """End the session and leave the device at its CLI prompt before the port is released.

        Measured on 1.5.1: StopSession is answered with an empty message, and the first byte the
        CLI then receives is swallowed. Waiting for the answer, then for a prompt after a bare
        return, leaves the device idle; reopening the port too soon after StopSession left a new
        session unanswered, and once left the device deaf until a restart."""
        self.rpc.cid += 1
        self._send(self.rpc.cid, 19, b"")
        try:
            for _raw in self._responses(self.rpc.cid, timeout):
                pass
            self.rpc._cli_until(b">: ", send=b"\r\r", timeout=timeout)  # one may be swallowed
        except (TimeoutError, ValueError, IndexError):
            print("bridge: the device did not confirm the end of the session", file=sys.stderr)

    def _send(self, cid, content_field, content, has_next=False):
        body = fs.field(1, 0, cid) + (fs.field(3, 0, 1) if has_next else b"")
        body += fs.field(content_field, 2, content)
        self.rpc.s.write(fs.varint(len(body)) + body)

    def _responses(self, cid, timeout):
        while True:
            raw = self.rpc.next_message(timeout)
            if raw.get(1, 0) != cid:
                continue  # a late answer to an earlier command
            yield raw
            if not raw.get(3, 0):
                return

    def request(self, content_field, content=b"", timeout=10.0, allow=(OK,)):
        """Send one request; return (status, [content of each response])."""
        self.rpc.cid += 1
        cid = self.rpc.cid
        self.pending = cid
        self._send(cid, content_field, content)
        parts, status = [], OK
        for raw in self._responses(cid, timeout):
            status = raw.get(2, OK)
            parts.append(raw)
        self.pending = 0
        if status not in allow:
            raise BridgeError(f"device refused request {content_field}: status {status}")
        return status, parts

    def device_info(self):
        _s, parts = self.request(DEVINFO_REQ)
        info = {}
        for raw in parts:
            msg = fields(raw.get(DEVINFO_RESP, b""))
            key = one(msg, 1, b"").decode("utf-8", "replace")
            if key:
                info[key] = one(msg, 2, b"").decode("utf-8", "replace")
        return info

    def locked(self):
        _s, parts = self.request(LOCK_REQ)
        return bool(one(fields(parts[-1].get(LOCK_RESP, b"")), 1, 0))

    def space(self, path="/ext"):
        _s, parts = self.request(INFO_REQ, text(1, path))
        msg = fields(parts[-1].get(INFO_RESP, b""))
        return one(msg, 1, 0), one(msg, 2, 0)

    def listdir(self, path):
        status, parts = self.request(LIST_REQ, text(1, path), allow=(OK, STORAGE_NOT_EXIST))
        if status == STORAGE_NOT_EXIST:
            return None
        files = []
        for raw in parts:
            for f in fields(raw.get(LIST_RESP, b"")).get(1, []):
                f = fields(f)
                files.append({"name": one(f, FILE_NAME, b"").decode("utf-8", "replace"),
                              "dir": one(f, FILE_TYPE, 0) == 1, "size": one(f, FILE_SIZE, 0)})
        return sorted(files, key=lambda f: f["name"])

    def md5(self, path):
        status, parts = self.request(MD5_REQ, text(1, path), timeout=MD5_TIMEOUT,
                                     allow=(OK, STORAGE_NOT_EXIST))
        if status == STORAGE_NOT_EXIST:
            return None
        return one(fields(parts[-1].get(MD5_RESP, b"")), 1, b"").decode("ascii")

    def read(self, path, timeout=30.0):
        status, parts = self.request(READ_REQ, text(1, path), timeout=timeout,
                                     allow=(OK, STORAGE_NOT_EXIST))
        if status == STORAGE_NOT_EXIST:
            return None
        data = b""
        for raw in parts:
            f = fields(one(fields(raw.get(READ_RESP, b"")), 1, b""))
            data += one(f, FILE_DATA, b"")
        return data


class Cli:
    """The device's text CLI, which installs packs: an appending write whose every chunk is
    acknowledged, and the storage and loader commands around it, all in one session.

    Measured on 1.5.1: switching from an RPC session to the CLI by reopening the port left the
    device deaf to both until a restart, so an install never mixes the two."""

    def __init__(self, port):
        import serial  # deferred: only the device path needs pyserial

        self.s = serial.Serial(port, 230400, timeout=0.2)
        self.buf, self.pending, self.owed = b"", False, 0
        self.until(b">: ", send=b"\x03\r")  # Ctrl-C ends any command left running
        while self.s.read(self.s.in_waiting or 1):
            pass  # drain any later prompt, so no command reads its predecessor's output
        self.buf = b""

    def until(self, tokens, send=None, timeout=None):
        """Bytes up to and including the earliest token; what follows stays buffered."""
        timeout = CHUNK_TIMEOUT if timeout is None else timeout
        if isinstance(tokens, bytes):
            tokens = (tokens,)
        if send:
            self.s.write(send)
        end = time.time() + timeout
        while time.time() < end:
            hits = [(self.buf.find(tok), tok) for tok in tokens if tok in self.buf]
            if hits:
                at, tok = min(hits)
                got, self.buf = self.buf[: at + len(tok)], self.buf[at + len(tok):]
                return got
            self.buf += self.s.read(self.s.in_waiting or 1)
        raise TimeoutError(f"the device CLI did not answer {tokens[0]!r} within {timeout} s")

    def write_chunk(self, path, data):
        command = f'storage write_chunk "{path}" {len(data)}\r'.encode("ascii")
        self.pending = True  # from the moment the line leaves: close() must wait for a prompt
        self.until(b"\n", send=command)  # the echoed command line, which may hold any text
        if self.until((b"Ready", b"Storage error")).endswith(b"Storage error"):
            detail = self.until(b"\r").decode("ascii", "replace").strip()
            self.until(b">: ")
            self.pending = False
            raise BridgeError(f"{path}: Storage error{detail}")
        self.owed = len(data)  # the device now waits for exactly this many bytes
        self.s.write(data)
        self.owed = 0
        after = self.until(b">: ")
        self.pending = False
        if b"Storage error" in after:  # the write itself failed, e.g. a full card
            detail = after[after.index(b"Storage error"):].split(b"\r")[0]
            raise BridgeError(f"{path}: {detail.decode('ascii', 'replace')}")

    def command(self, line, timeout=CHUNK_TIMEOUT):
        """One command; its output without the echo and the prompt."""
        self.pending = True
        self.until(b"\n", send=line.encode("ascii") + b"\r")  # the echoed command line
        out = self.until(b">: ", timeout=timeout)[:-3].decode("ascii", "replace").strip()
        self.pending = False
        return out

    def storage(self, verb, *paths, accept=(), timeout=MD5_TIMEOUT):
        """`storage <verb> "<path>"...`; None when the error is one of `accept`."""
        out = self.command(f"storage {verb} " + " ".join(f'"{p}"' for p in paths), timeout)
        if "Storage error" not in out:
            return out
        detail = out[out.index("Storage error"):].splitlines()[0]
        if any(a in detail for a in accept):
            return None
        raise BridgeError(f"storage {verb} {paths[0]}: {detail}")

    def md5(self, path):
        out = self.storage("md5", path, accept=("not exist",))
        return out.split()[-1] if out else None

    def app_running(self):
        return "No application" not in self.command("loader info")

    def close(self, grace=MD5_TIMEOUT):
        """Release the port, but not under a running command: a `storage rename` abandoned
        after 60 s and the port closed under it left the device deaf until a restart. A chunk
        cut off after "Ready" is completed with zero bytes first, since the device waits for
        them; the part is then wrong, and the MD5 check of the next push removes it."""
        if self.owed:
            self.s.write(bytes(self.owed))
            self.owed = 0
        if self.pending:
            try:
                self.until(b">: ", timeout=grace)
                self.pending = False
            except TimeoutError:
                print("bridge: the device never finished its last command; it may need a "
                      "restart (hold Left and Back)", file=sys.stderr)
        self.s.close()


def md5_of(data):
    return hashlib.md5(data).hexdigest()  # what the device's Md5sumRequest computes


def connect(args):
    args.port = args.port or fs.find_port()
    return Device(args.port)


def pack_state(local, size, remote_size, remote_md5):
    if local is None:
        return "no local pack to compare"
    if remote_size is None:
        return "not installed"
    if remote_md5 is not None:
        return "current" if remote_md5 == local else "differs from the local pack"
    if remote_size != size:
        return "differs from the local pack"
    return "same size (run with --md5 to compare contents)"


def cmd_status(args):
    pack = Path(args.pack)
    data = pack.read_bytes() if pack.exists() else None
    local = md5_of(data) if data is not None else None
    dev = connect(args)
    try:
        info = dev.device_info()
        total, free = dev.space()
        files = dev.listdir(APP_DIR) or []
        apps = dev.listdir(FAP_PATH.rsplit("/", 1)[0]) or []
        remote_size = next((f["size"] for f in files if f["name"] == pack.name), None)
        remote_md5 = None
        if args.md5 and remote_size is not None:
            remote_md5 = dev.md5(f"{APP_DIR}/{pack.name}")
        locked = dev.locked()
    finally:
        dev.close()
    state = pack_state(local, len(data) if data is not None else None, remote_size, remote_md5)
    report = {
        "device": {k: info.get(k) for k in ("hardware_name", "firmware_version",
                                            "firmware_commit", "protobuf_version_major",
                                            "protobuf_version_minor")},
        "sd": {"total": total, "free": free},
        "app_installed": any(f["name"] == FAP_PATH.rsplit("/", 1)[1] for f in apps),
        "app_running": locked,
        "files": files,
        "pack": {"name": pack.name, "local_md5": local, "device_md5": remote_md5,
                 "device_size": remote_size, "state": state},
    }
    print(json.dumps(report, indent=1, sort_keys=True))
    return 0


def progress_line(done, total):
    print(f"\r{done * 100 // total:3d}% of {total} bytes", end="", file=sys.stderr)


def cmd_push_pack(args):
    """One CLI session: check, write the part chunk by acknowledged chunk, verify, swap."""
    pack = Path(args.pack)
    if not re.fullmatch(r"[A-Za-z0-9._-]+\.pkp", pack.name):
        raise BridgeError(f"{pack.name}: a pack is a .pkp named with letters, digits, . _ - only")
    data = pack.read_bytes()
    pocket.decode(data)  # never install a pack the reader would refuse
    local = md5_of(data)
    target, tmp = f"{APP_DIR}/{pack.name}", f"{APP_DIR}/{pack.name}.part"
    cli = Cli(args.port or fs.find_port())
    try:
        if cli.app_running():
            raise BridgeError("an application is running on the Flipper; close it first")
        if not args.force and cli.md5(target) == local:
            print(f"{target}: already current (md5 {local})")
            return 0
        cli.storage("mkdir", APP_DIR, accept=("already exist",))
        cli.storage("remove", tmp, accept=("not exist",))  # write_chunk appends
        for at in range(0, len(data), CHUNK):
            cli.write_chunk(tmp, data[at: at + CHUNK])
            progress_line(min(at + CHUNK, len(data)), len(data))
        print(file=sys.stderr)
        got = cli.md5(tmp)
        if got != local:
            cli.storage("remove", tmp, accept=("not exist",))
            raise BridgeError(f"md5 after the write is {got}, expected {local}; nothing replaced")
        if cli.app_running():
            raise BridgeError(f"an application started during the upload; nothing replaced, the "
                              f"verified pack waits as {tmp}")
        cli.storage("remove", target, accept=("not exist",))
        try:
            cli.storage("rename", tmp, target)
        except BridgeError as e:
            raise BridgeError(f"{e}; the old pack is gone and the verified pack is {tmp}: "
                              f"rename it to {pack.name}, or run push-pack again") from e
        if cli.md5(target) != local:
            raise BridgeError(f"{target}: md5 differs after the rename")
    finally:
        cli.close()
    print(f"{target}: installed, {len(data)} bytes, md5 {local}")
    return 0


def cmd_pull(args):
    out = Path(args.out_dir)
    out.mkdir(parents=True, exist_ok=True)
    dev = connect(args)
    got = {}
    try:
        for name in PULLED:
            data = dev.read(f"{APP_DIR}/{name}")
            if data is not None:
                (out / name).write_bytes(data)
                got[name] = {"bytes": len(data), "md5": md5_of(data)}
    finally:
        dev.close()
    print(json.dumps({"out": str(out), "files": got}, indent=1, sort_keys=True))
    return 0


def load_pack(path):
    p = Path(path)
    if not p.exists():
        return None
    return pocket.decode(p.read_bytes())


def resolve(objects, oid, index):
    """The object a session names: by stable id, at its recorded index first."""
    if objects is None:
        return None
    if 0 <= index < len(objects) and objects[index]["id"] == oid:
        return objects[index]
    return next((o for o in objects if o["id"] == oid), None)


def parse_session(line):
    """One line of sessions.jsonl, as the device writes it; None for a torn or foreign line."""
    try:
        s = json.loads(line)
    except ValueError:
        return None
    if not isinstance(s, dict) or s.get("v") != 1 or not isinstance(s.get("c"), list):
        return None
    cands = s["c"]
    if not all(isinstance(c, dict) and isinstance(c.get("id"), str)
               and isinstance(c.get("i"), int) and isinstance(c.get("m"), str)
               and set(c["m"]) <= set(MARKS) for c in cands):
        return None
    return s


def decode_sessions(raw, pack):
    objects = pack["objects"] if pack else None
    sessions, skipped = [], 0
    for line in raw.decode("utf-8", "replace").splitlines():
        if not line.strip():
            continue
        s = parse_session(line)
        if s is None:
            skipped += 1
            continue
        cands = []
        for c in s["c"]:
            o = resolve(objects, c["id"], c["i"])
            markers = [it["text"] for it in o["items"] if it["type"] == 1] if o else []
            marks = [{"state": MARKS[m], "marker": markers[k] if k < len(markers) else None}
                     for k, m in enumerate(c["m"])]
            counts = {v: 0 for v in MARKS.values()}
            for m in c["m"]:
                counts[MARKS[m]] += 1
            cands.append({"id": c["id"], "index": c["i"], "found": o is not None,
                          "title": o["title"] if o else None, "counts": counts, "marks": marks})
        sessions.append({"time": s.get("t"), "pack": s.get("pack"), "candidates": cands})
    return {"sessions": sessions, "skipped_lines": skipped,
            "note": "the reader's own marks; counts, never a score"}


def fnv1a(data):
    h = 0x811C9DC5
    for b in data:
        h = ((h ^ b) * 0x01000193) & 0xFFFFFFFF
    return h


def decode_progress_image(d):
    """One progress file image, as app/core/progress.c checks it; None when it is not valid."""
    if d is None or len(d) < PROGRESS_HEADER + 4 or d[:4] != b"PKPS":
        return None
    version, _r, generation, session, count = struct.unpack_from("<HHIHH", d, 4)
    if version != PROGRESS_VERSION or count > PROGRESS_CAP:
        return None
    body = PROGRESS_HEADER + count * PROGRESS_ENTRY
    if len(d) < body + 4 or zlib.crc32(d[:body]) != struct.unpack_from("<I", d, body)[0]:
        return None
    entries = []
    for i in range(count):
        key, quiz, box, right, attempts, due = struct.unpack_from(
            "<IHBBHH", d, PROGRESS_HEADER + i * PROGRESS_ENTRY)
        entries.append({"key": key, "quiz": quiz, "box": box if 1 <= box <= 5 else 1,
                        "last_right": bool(right), "attempts": attempts, "due": due})
    return {"generation": generation, "session": session, "entries": entries}


def decode_progress(a, b, pack, names=("progress.bin", "progress.tmp")):
    """The better of the two copies, as the app loads it, with each question's prompt."""
    pa, pb = decode_progress_image(a), decode_progress_image(b)
    taken = pb if pb and (not pa or pb["generation"] > pa["generation"]) else pa
    if taken is None:
        return {"valid": False, "entries": []}
    prompts = {}
    if pack:
        for q in pack["quiz"]:
            prompts.setdefault(fnv1a(q["prompt"].encode("latin-1")), q["prompt"])
    boxes = {str(n): 0 for n in range(1, 6)}
    for e in taken["entries"]:
        boxes[str(e["box"])] += 1
        e["due_now"] = e["due"] <= taken["session"]
        e["prompt"] = prompts.get(e["key"])
    return {"valid": True, "from": names[0] if taken is pa else names[1],
            "generation": taken["generation"], "session": taken["session"], "boxes": boxes,
            "due_now": sum(e["due_now"] for e in taken["entries"]), "entries": taken["entries"]}


def read_optional(path):
    return Path(path).read_bytes() if path and Path(path).exists() else None


def cmd_sessions(args):
    report = decode_sessions(Path(args.file).read_bytes(), load_pack(args.pack))
    print(json.dumps(report, ensure_ascii=False, indent=1))
    return 0


def cmd_progress(args):
    names = (Path(args.file).name, Path(args.other).name if args.other else None)
    report = decode_progress(read_optional(args.file), read_optional(args.other),
                             load_pack(args.pack), names)
    print(json.dumps(report, ensure_ascii=False, indent=1))
    return 0 if report["valid"] else 1


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    st = sub.add_parser("status")
    st.add_argument("--pack", default=str(DEFAULT_PACK))
    st.add_argument("--md5", action="store_true",
                    help="hash the installed pack on the device (minutes for a large pack)")
    pp = sub.add_parser("push-pack")
    pp.add_argument("pack", nargs="?", default=str(DEFAULT_PACK))
    pp.add_argument("--force", action="store_true", help="write even when the md5 matches")
    pl = sub.add_parser("pull")
    pl.add_argument("out_dir")
    for p in (st, pp, pl):
        p.add_argument("--port")
    se = sub.add_parser("sessions")
    se.add_argument("file")
    pr = sub.add_parser("progress")
    pr.add_argument("file")
    pr.add_argument("other", nargs="?", help="progress.tmp, when it was pulled too")
    for p in (se, pr):
        p.add_argument("--pack", default=str(DEFAULT_PACK))
    args = ap.parse_args(argv)
    run = {"status": cmd_status, "push-pack": cmd_push_pack, "pull": cmd_pull,
           "sessions": cmd_sessions, "progress": cmd_progress}[args.cmd]
    try:
        return run(args)
    except (BridgeError, pocket.PocketError, OSError, ValueError, IndexError) as e:
        print(f"bridge: {e}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
