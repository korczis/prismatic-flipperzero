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
import struct
import sys
import zlib
from pathlib import Path

import flipper_screen as fs
import pocket

ROOT = Path(__file__).resolve().parent.parent
APP_DIR = "/ext/apps_data/prismatic"
FAP_PATH = "/ext/apps/Tools/prismatic.fap"
DEFAULT_PACK = ROOT / "build" / "packs" / "kompendium.pkp"
PULLED = ("sessions.jsonl", "progress.bin", "progress.tmp")
CHUNK = 512
# The device hashes a 20 MB pack slowly; one request waits this long for the answer.
MD5_TIMEOUT = 600.0

# PB.Main content fields (flipper.proto) and their payload fields (storage.proto, system.proto).
LIST_REQ, LIST_RESP, READ_REQ, READ_RESP, WRITE_REQ = 7, 8, 9, 10, 11
DELETE_REQ, MKDIR_REQ, MD5_REQ, MD5_RESP = 12, 13, 14, 15
LOCK_REQ, LOCK_RESP, STAT_REQ, STAT_RESP = 17, 18, 24, 25
INFO_REQ, INFO_RESP, RENAME_REQ, DEVINFO_REQ, DEVINFO_RESP = 28, 29, 30, 32, 33
OK, STORAGE_EXIST, STORAGE_NOT_EXIST = 0, 6, 7
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

    def __init__(self, port):
        self.rpc = fs.Rpc(port)

    def close(self):
        self.rpc.s.write(fs.main_msg(self.rpc.cid + 1, 19, b""))  # StopSession: back to the CLI
        self.rpc.s.close()

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
        self._send(cid, content_field, content)
        parts, status = [], OK
        for raw in self._responses(cid, timeout):
            status = raw.get(2, OK)
            parts.append(raw)
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

    def write(self, path, data, progress=None):
        """One WriteRequest split into chunks under one command id, has_next on all but the last."""
        self.rpc.cid += 1
        cid = self.rpc.cid
        chunks = [data[i: i + CHUNK] for i in range(0, len(data), CHUNK)] or [b""]
        for n, chunk in enumerate(chunks, 1):
            content = text(1, path) + fs.field(2, 2, fs.field(FILE_DATA, 2, chunk))
            self._send(cid, WRITE_REQ, content, has_next=n < len(chunks))
            if progress and (n % 256 == 0 or n == len(chunks)):
                progress(n * CHUNK if n < len(chunks) else len(data), len(data))
        status = OK
        for raw in self._responses(cid, timeout=60.0):
            status = raw.get(2, OK)
        if status != OK:
            raise BridgeError(f"device refused the write of {path}: status {status}")

    def mkdir(self, path):
        self.request(MKDIR_REQ, text(1, path), allow=(OK, STORAGE_EXIST))

    def delete(self, path):
        self.request(DELETE_REQ, text(1, path), allow=(OK, STORAGE_NOT_EXIST))

    def rename(self, old, new):
        self.request(RENAME_REQ, text(1, old) + text(2, new))


def md5_of(data):
    return hashlib.md5(data).hexdigest()  # what the device's Md5sumRequest computes


def connect(args):
    return Device(args.port or fs.find_port())


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


def cmd_push_pack(args):
    pack = Path(args.pack)
    if pack.suffix != ".pkp":
        raise BridgeError(f"{pack}: only a .pkp pack is installed")
    data = pack.read_bytes()
    pocket.decode(data)  # never install a pack the reader would refuse
    local = md5_of(data)
    target, tmp = f"{APP_DIR}/{pack.name}", f"{APP_DIR}/{pack.name}.part"
    dev = connect(args)
    try:
        if dev.locked():
            raise BridgeError("an application is running on the Flipper; close it first")
        if not args.force and dev.md5(target) == local:
            print(f"{target}: already current (md5 {local})")
            return 0
        dev.mkdir(APP_DIR)
        dev.delete(tmp)

        def progress(done, total):
            print(f"\r{done * 100 // total:3d}% of {total} bytes", end="", file=sys.stderr)

        dev.write(tmp, data, progress)
        print(file=sys.stderr)
        got = dev.md5(tmp)
        if got != local:
            dev.delete(tmp)
            raise BridgeError(f"md5 after the write is {got}, expected {local}; nothing replaced")
        dev.delete(target)
        dev.rename(tmp, target)
        if dev.md5(target) != local:
            raise BridgeError(f"{target}: md5 differs after the rename")
    finally:
        dev.close()
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


def decode_progress(a, b, pack):
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
    return {"valid": True, "from": "progress.bin" if taken is pa else "progress.tmp",
            "generation": taken["generation"], "session": taken["session"], "boxes": boxes,
            "due_now": sum(e["due_now"] for e in taken["entries"]), "entries": taken["entries"]}


def read_optional(path):
    return Path(path).read_bytes() if path and Path(path).exists() else None


def cmd_sessions(args):
    report = decode_sessions(Path(args.file).read_bytes(), load_pack(args.pack))
    print(json.dumps(report, ensure_ascii=False, indent=1))
    return 0


def cmd_progress(args):
    report = decode_progress(read_optional(args.file), read_optional(args.other),
                             load_pack(args.pack))
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
    except (BridgeError, pocket.PocketError, TimeoutError) as e:
        print(f"bridge: {e}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
