"""Tests for tools/bridge.py against a fake Flipper that speaks the RPC storage subset."""

import hashlib
import io
import json
import re
import struct
import sys
import tempfile
import types
import unittest
import zlib
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(Path(__file__).parent))

import bridge  # noqa: E402
import flipper_screen as fs  # noqa: E402
import pocket  # noqa: E402
from test_pocket import tiny_ir  # noqa: E402

F = fs.field


def text(n, s):
    return F(n, 2, s.encode())


def file_msg(name, size, data=None, is_dir=False):
    body = (F(1, 0, 1) if is_dir else b"") + text(2, name) + F(3, 0, size)
    return body + (F(4, 2, data) if data is not None else b"")


class FakeDevice:
    """A Flipper over USB: the CLI prompt, then RPC framing; files live in a dict."""

    files = {}
    dirs = set()
    locked = False
    mode = ""  # "bad-md5", "bad-rename", "cli-error", "refuse-info", "drop-first", "mute"

    def __init__(self, port, baud, timeout):
        self.out, self.rx, self.rpc, self.writes, self.closed = bytearray(), b"", False, {}, False
        self.dropped, self.expect, self.target, self.stopped = False, 0, "", 0

    @property
    def in_waiting(self):
        return len(self.out)

    def read(self, n):
        chunk, self.out[:] = bytes(self.out[:n]), self.out[n:]
        return chunk

    def reset_input_buffer(self):
        self.out.clear()

    def close(self):
        self.closed = True

    def reply(self, cid, content_field=None, content=b"", status=0, has_next=False):
        body = F(1, 0, cid) + (F(2, 0, status) if status else b"") + (F(3, 0, 1) if has_next
                                                                        else b"")
        if content_field is not None:
            body += F(content_field, 2, content)
        self.out += fs.varint(len(body)) + body

    def write(self, data):
        if self.expect:  # the raw bytes of a write_chunk; split arrivals are allowed
            take, rest = data[: self.expect], data[self.expect:]
            self.files[self.target] = self.files.get(self.target, b"") + take
            self.expect -= len(take)
            if not self.expect:
                full = self.mode == "full"
                self.out += b"\r\nStorage error: out of space\r\n\r\n>: " if full else b"\r\n>: "
            data = rest
            if not data:
                return
        if not self.rpc:
            m = re.fullmatch(rb'storage write_chunk "([^"]+)" (\d+)\r', data)
            if m:
                self.out += data + b"\n"  # the CLI echoes the command line
                if self.mode == "cli-error":
                    self.out += b"Storage error: access denied\r\n\r\n>: "
                    return
                self.target, self.expect = m.group(1).decode(), int(m.group(2))
                self.out += b"Ready\r\n"
                return
            if data == b"\x03\r":
                self.out += b"\r\n>: "
            elif data == b"start_rpc_session\r":
                self.out += b"start_rpc_session\r\n"
                self.rpc = True
            return
        self.rx += data
        while self.rx:
            ln, i = fs.read_varint(self.rx, 0)
            msg, self.rx = self.rx[i: i + ln], self.rx[i + ln:]
            self.handle(bridge.fields(msg))

    def handle(self, m):
        cid, more = bridge.one(m, 1, 0), bridge.one(m, 3, 0)
        arg = {k: bridge.fields(v[0]) for k, v in m.items() if k > 3}
        path = lambda a, n=1: bridge.one(a, n, b"").decode()  # noqa: E731
        if self.mode == "mute" or (self.mode == "drop-first" and not self.dropped):
            self.dropped = True  # the first request after start_rpc_session, lost
            return
        self.reply(cid + 1000)  # a stray answer to some other command, which must be skipped
        if bridge.PING_REQ in arg:
            self.reply(cid, 6, b"")
        elif bridge.DEVINFO_REQ in arg:
            pairs = [("hardware_name", "Iruqli"), ("firmware_version", "1.5.1"), ("", "")]
            for k, (key, val) in enumerate(pairs):
                self.reply(cid, bridge.DEVINFO_RESP, text(1, key) + text(2, val),
                           has_next=k < len(pairs) - 1)
        elif bridge.INFO_REQ in arg:
            if self.mode == "refuse-info":
                self.reply(cid, status=5)
            else:
                self.reply(cid, bridge.INFO_RESP, F(1, 0, 1000) + F(2, 0, 400))
        elif bridge.LOCK_REQ in arg:
            later = self.mode == "lock-later" and any(p.endswith(".part") for p in self.files)
            self.reply(cid, bridge.LOCK_RESP, F(1, 0, 1) if self.locked or later else b"")
        elif bridge.LIST_REQ in arg:
            d = path(arg[bridge.LIST_REQ])
            if d not in self.dirs:
                return self.reply(cid, status=bridge.STORAGE_NOT_EXIST)
            names = sorted(p for p in self.files if p.rsplit("/", 1)[0] == d)
            entries = [F(1, 2, file_msg(p.rsplit("/", 1)[1], len(self.files[p])))
                       for p in names] + [F(1, 2, file_msg("sub", 0, is_dir=True))]
            self.reply(cid, bridge.LIST_RESP, entries[0], has_next=True)
            self.reply(cid, bridge.LIST_RESP, b"".join(entries[1:]))
        elif bridge.MD5_REQ in arg:
            p = path(arg[bridge.MD5_REQ])
            if p not in self.files:
                return self.reply(cid, status=bridge.STORAGE_NOT_EXIST)
            digest = hashlib.md5(self.files[p]).hexdigest()
            if self.mode == "bad-md5" and p.endswith(".part"):
                digest = "0" * 32
            self.reply(cid, bridge.MD5_RESP, text(1, digest))
        elif bridge.READ_REQ in arg:
            p = path(arg[bridge.READ_REQ])
            if p not in self.files:
                return self.reply(cid, status=bridge.STORAGE_NOT_EXIST)
            data = self.files[p]
            parts = [data[i: i + 512] for i in range(0, len(data), 512)] or [b""]
            for k, part in enumerate(parts):
                self.reply(cid, bridge.READ_RESP, F(1, 2, file_msg("x", len(part), part)),
                           has_next=k < len(parts) - 1)
        elif bridge.DELETE_REQ in arg:
            p = path(arg[bridge.DELETE_REQ])
            if self.files.pop(p, None) is None:
                return self.reply(cid, status=bridge.STORAGE_NOT_EXIST)
            self.reply(cid)
        elif bridge.MKDIR_REQ in arg:
            d = path(arg[bridge.MKDIR_REQ])
            if d in self.dirs:
                return self.reply(cid, status=bridge.STORAGE_EXIST)
            self.dirs.add(d)
            self.reply(cid)
        elif bridge.RENAME_REQ in arg:
            a = arg[bridge.RENAME_REQ]
            if self.mode == "refuse-rename":
                return self.reply(cid, status=9)
            data = self.files.pop(path(a))
            self.files[path(a, 2)] = data + (b"!" if self.mode == "bad-rename" else b"")
            self.reply(cid)
        elif 19 in m:
            self.rpc = False
            type(self).stops = getattr(type(self), "stops", 0) + 1


def pack_bytes():
    return pocket.encode(tiny_ir())


def run(argv, **state):
    for k, v in {"files": {}, "dirs": set(), "locked": False, "mode": ""}.items():
        setattr(FakeDevice, k, state.get(k, v))
    out, err = io.StringIO(), io.StringIO()
    with mock.patch.dict(sys.modules, {"serial": types.SimpleNamespace(Serial=FakeDevice)}), \
            mock.patch.object(fs, "find_port", lambda: "/dev/fake"), \
            redirect_stdout(out), redirect_stderr(err):
        rc = bridge.main(argv)
    return rc, out.getvalue(), err.getvalue()


def progress_image(entries, generation=3, session=7, version=1, crc_ok=True):
    body = b"PKPS" + struct.pack("<HHIHHI", version, 0, generation, session, len(entries), 0)
    for key, quiz, box, right, attempts, due in entries:
        body += struct.pack("<IHBBHH", key, quiz, box, right, attempts, due)
    crc = zlib.crc32(body) ^ (0 if crc_ok else 1)
    return body + struct.pack("<I", crc)


class DeviceTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.pack = Path(self.tmp.name) / "kompendium.pkp"
        self.pack.write_bytes(pack_bytes())

    def tearDown(self):
        self.tmp.cleanup()

    def status(self, files, *extra, **state):
        dirs = {bridge.APP_DIR, "/ext/apps/Tools"}
        rc, out, err = run(["status", "--pack", str(self.pack), *extra], files=files, dirs=dirs,
                           **state)
        self.assertEqual(rc, 0, err)
        return json.loads(out)

    def test_status_reports_device_files_and_pack_state(self):
        app = bridge.APP_DIR
        data = self.pack.read_bytes()
        r = self.status({f"{app}/kompendium.pkp": data, bridge.FAP_PATH: b"fap"})
        self.assertEqual(r["device"]["firmware_version"], "1.5.1")
        self.assertEqual(r["sd"], {"total": 1000, "free": 400})
        self.assertTrue(r["app_installed"])
        self.assertFalse(r["app_running"])
        self.assertEqual([f["name"] for f in r["files"]], ["kompendium.pkp", "sub"])
        self.assertTrue(r["files"][1]["dir"])
        self.assertTrue(r["pack"]["state"].startswith("same size"))
        r = self.status({f"{app}/kompendium.pkp": data}, "--md5", locked=True)
        self.assertEqual(r["pack"]["state"], "current")
        self.assertTrue(r["app_running"])
        self.assertFalse(r["app_installed"])
        r = self.status({f"{app}/kompendium.pkp": data[:-1] + b"?"}, "--md5")
        self.assertEqual(r["pack"]["state"], "differs from the local pack")
        r = self.status({f"{app}/kompendium.pkp": b"short"})
        self.assertEqual(r["pack"]["state"], "differs from the local pack")
        self.assertEqual(self.status({})["pack"]["state"], "not installed")
        self.pack.unlink()
        self.assertEqual(self.status({})["pack"]["state"], "no local pack to compare")

    def test_status_without_the_app_folder(self):
        rc, out, _ = run(["status", "--pack", str(self.pack), "--port", "/dev/x"])
        self.assertEqual(rc, 0)
        self.assertEqual(json.loads(out)["files"], [])

    def test_a_refused_request_is_an_error_not_a_traceback(self):
        rc, _, err = run(["status", "--pack", str(self.pack)], mode="refuse-info")
        self.assertEqual(rc, 1)
        self.assertIn("status 5", err)

    def test_push_pack_installs_through_a_temporary_name(self):
        rc, out, err = run(["push-pack", str(self.pack)])
        self.assertEqual(rc, 0, err)
        target = f"{bridge.APP_DIR}/kompendium.pkp"
        self.assertEqual(FakeDevice.files, {target: self.pack.read_bytes()})
        self.assertIn("installed", out)
        self.assertIn("100%", err)
        rc, out, _ = run(["push-pack", str(self.pack)], files=dict(FakeDevice.files),
                         dirs={bridge.APP_DIR})
        self.assertEqual(rc, 0)
        self.assertIn("already current", out)
        rc, out, _ = run(["push-pack", str(self.pack), "--force"], files=dict(FakeDevice.files),
                         dirs={bridge.APP_DIR})
        self.assertIn("installed", out)
        # the CLI echoes the command: a pack named like a CLI answer does not confuse the reader
        odd = Path(self.tmp.name) / "Ready_Storage_error.pkp"
        odd.write_bytes(self.pack.read_bytes())
        rc, _, err = run(["push-pack", str(odd)])
        self.assertEqual(rc, 0, err)
        self.assertEqual(FakeDevice.files[f"{bridge.APP_DIR}/{odd.name}"], odd.read_bytes())
        # a leftover .part from an interrupted push is replaced
        part = target + ".part"
        rc, _, _ = run(["push-pack", str(self.pack)], files={part: b"old"})
        self.assertEqual(rc, 0)
        self.assertNotIn(part, FakeDevice.files)

    def test_push_pack_refusals(self):
        rc, _, err = run(["push-pack", str(self.pack)], locked=True)
        self.assertEqual((rc, FakeDevice.files), (1, {}))
        self.assertIn("close it first", err)
        rc, _, err = run(["push-pack", str(self.pack)], mode="bad-md5")
        self.assertEqual((rc, FakeDevice.files), (1, {}))
        self.assertIn("nothing replaced", err)
        rc, _, err = run(["push-pack", str(self.pack)], mode="bad-rename")
        self.assertEqual(rc, 1)
        self.assertIn("after the rename", err)
        rc, _, err = run(["push-pack", str(self.pack)], mode="cli-error")
        self.assertEqual((rc, FakeDevice.files), (1, {}))
        self.assertIn("Storage error: access denied", err)
        rc, _, err = run(["push-pack", str(self.pack)], mode="full")
        self.assertEqual(rc, 1)
        self.assertIn("out of space", err)
        rc, _, err = run(["push-pack", str(self.pack)], mode="lock-later")
        self.assertEqual(rc, 1)
        self.assertIn("verified pack waits", err)
        self.assertNotIn(f"{bridge.APP_DIR}/kompendium.pkp", FakeDevice.files)
        rc, _, err = run(["push-pack", str(self.pack)], mode="refuse-rename")
        self.assertEqual(rc, 1)
        self.assertIn("the verified pack is", err)
        spaced = Path(self.tmp.name) / "my pack.pkp"
        spaced.write_bytes(self.pack.read_bytes())
        rc, _, err = run(["push-pack", str(spaced)])
        self.assertEqual((rc, FakeDevice.files), (1, {}))
        self.assertIn("letters, digits", err)
        rc, _, err = run(["push-pack", str(Path(self.tmp.name) / "absent.pkp")])
        self.assertEqual(rc, 1)
        self.assertIn("No such file", err)
        other = Path(self.tmp.name) / "notes.txt"
        other.write_text("x")
        self.assertEqual(run(["push-pack", str(other)])[0], 1)
        broken = Path(self.tmp.name) / "broken.pkp"
        broken.write_bytes(b"PKP0")
        rc, _, err = run(["push-pack", str(broken)])
        self.assertEqual((rc, FakeDevice.files), (1, {}))
        self.assertIn("truncated", err)

    def test_pull_copies_only_what_exists(self):
        app = bridge.APP_DIR
        files = {f"{app}/sessions.jsonl": b"x" * 1500, f"{app}/progress.bin": b"p"}
        out_dir = Path(self.tmp.name) / "pulled"
        rc, out, _ = run(["pull", str(out_dir)], files=files)
        self.assertEqual(rc, 0)
        got = json.loads(out)["files"]
        self.assertEqual(sorted(got), ["progress.bin", "sessions.jsonl"])
        self.assertEqual((out_dir / "sessions.jsonl").read_bytes(), b"x" * 1500)
        self.assertFalse((out_dir / "progress.tmp").exists())

    def test_device_skips_empty_device_info_keys_and_empty_reads(self):
        FakeDevice.files = {"/ext/empty": b""}
        with mock.patch.dict(sys.modules, {"serial": types.SimpleNamespace(Serial=FakeDevice)}):
            dev = bridge.Device("/dev/fake")
        self.assertNotIn("", dev.device_info())
        self.assertEqual(dev.read("/ext/empty"), b"")
        self.assertIsNone(dev.read("/ext/none"))
        self.assertIsNone(dev.md5("/ext/none"))
        dev.close()
        self.assertTrue(dev.rpc.s.closed)

    def test_a_session_is_never_ended_inside_a_request(self):
        with mock.patch.dict(sys.modules, {"serial": types.SimpleNamespace(Serial=FakeDevice)}):
            FakeDevice.mode, FakeDevice.stops = "", 0
            dev = bridge.Device("/dev/fake")
            FakeDevice.mode = "mute"
            with self.assertRaises(TimeoutError):
                dev.request(bridge.PING_REQ, timeout=0.2)
            err = io.StringIO()
            with redirect_stderr(err):
                dev.close(grace=0.2)  # it never answers: the port is released, no StopSession
            self.assertIn("never answered", err.getvalue())
            self.assertEqual(FakeDevice.stops, 0)
            self.assertTrue(dev.rpc.s.closed)
            FakeDevice.mode = ""
            dev = bridge.Device("/dev/fake")
            FakeDevice.mode = "mute"
            with self.assertRaises(TimeoutError):
                dev.request(bridge.PING_REQ, timeout=0.2)
            dev.rpc.s.reply(dev.pending, 6, b"")  # the late answer arrives
            FakeDevice.mode = ""
            dev.close(grace=1.0)
            self.assertEqual((dev.pending, FakeDevice.stops), (0, 1))
        FakeDevice.mode = ""

    def test_cli_chunks_are_acknowledged_and_a_silent_cli_times_out(self):
        FakeDevice.files, FakeDevice.mode = {}, ""
        data = bytes(range(256)) * 70  # three chunks, the last one short
        seen = []
        with mock.patch.dict(sys.modules, {"serial": types.SimpleNamespace(Serial=FakeDevice)}):
            bridge.send_file("/dev/fake", "/ext/f.part", data, lambda d, n: seen.append(d))
            self.assertEqual(FakeDevice.files["/ext/f.part"], data)
            self.assertEqual(seen, [8192, 16384, len(data)])
            cli = bridge.Cli("/dev/fake")
            with self.assertRaises(TimeoutError):
                cli.until(b"never", timeout=0.2)
            cli.close()

    def test_a_lost_first_request_is_retried_by_ping(self):
        with mock.patch.dict(sys.modules, {"serial": types.SimpleNamespace(Serial=FakeDevice)}):
            FakeDevice.mode = "drop-first"
            dev = bridge.Device("/dev/fake")
            self.assertTrue(dev.rpc.s.dropped)
            self.assertEqual(dev.device_info()["firmware_version"], "1.5.1")
            FakeDevice.mode = "mute"
            with self.assertRaises(bridge.BridgeError):
                bridge.Device("/dev/fake", tries=1)
        FakeDevice.mode = ""


class DecodeTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.pack = Path(self.tmp.name) / "p.pkp"
        self.pack.write_bytes(pack_bytes())
        self.objects = pocket.decode(self.pack.read_bytes())["objects"]

    def tearDown(self):
        self.tmp.cleanup()

    def write(self, name, data):
        p = Path(self.tmp.name) / name
        p.write_bytes(data)
        return str(p)

    def test_sessions_name_markers_and_count_never_score(self):
        with_marker = next(i for i, o in enumerate(self.objects)
                           if any(it["type"] == 1 for it in o["items"]))
        oid = self.objects[with_marker]["id"]
        lines = [
            {"v": 1, "t": "2026-11-26T10:00:00", "pack": "t 1",
             "c": [{"id": oid, "i": with_marker, "m": "OA"}]},
            {"v": 1, "t": "2026-11-26T10:05:00", "pack": "t 1",
             "c": [{"id": oid, "i": 99, "m": "U"}, {"id": "gone/1", "i": 0, "m": "N-"}]},
        ]
        raw = "\n".join(json.dumps(x) for x in lines) + "\n\n"
        raw += '{"v":1,"t":"torn'  # power lost mid-append
        raw += '\n{"v":2,"c":[]}\n{"v":1,"c":[{"id":"x","i":0,"m":"Z"}]}\n[1]\n'
        f = self.write("sessions.jsonl", raw.encode())
        rc, out, _ = run(["sessions", f, "--pack", str(self.pack)])
        self.assertEqual(rc, 0)
        r = json.loads(out)
        self.assertEqual(r["skipped_lines"], 4)
        first, second = r["sessions"]
        c = first["candidates"][0]
        self.assertEqual((c["found"], c["counts"]["observed"]), (True, 1))
        self.assertEqual(c["marks"][0], {"state": "observed", "marker": "ind"})
        self.assertIsNone(c["marks"][1]["marker"])  # more marks than the pack has markers
        self.assertTrue(second["candidates"][0]["found"])  # found by id, index moved
        self.assertEqual(second["candidates"][1]["title"], None)
        self.assertNotIn("score", json.dumps(r["sessions"]))
        rc, out, _ = run(["sessions", f, "--pack", str(Path(self.tmp.name) / "none.pkp")])
        self.assertFalse(json.loads(out)["sessions"][0]["candidates"][0]["found"])

    def test_fnv1a_matches_the_reference_vectors(self):
        self.assertEqual(bridge.fnv1a(b""), 0x811C9DC5)
        self.assertEqual(bridge.fnv1a(b"a"), 0xE40C292C)
        self.assertEqual(bridge.fnv1a(b"foobar"), 0xBF9CF968)

    def test_progress_takes_the_newer_valid_copy_and_names_prompts(self):
        prompt = pocket.decode(self.pack.read_bytes())["quiz"][0]["prompt"]
        key = bridge.fnv1a(prompt.encode("latin-1"))
        a = self.write("progress.bin", progress_image([(key, 0, 2, 1, 3, 5), (1, 4, 9, 0, 1, 8)]))
        b = self.write("progress.tmp", progress_image([(key, 0, 3, 1, 4, 9)], generation=4))
        rc, out, _ = run(["progress", a, "--pack", str(self.pack)])
        self.assertEqual(rc, 0)
        r = json.loads(out)
        self.assertEqual((r["from"], r["generation"], r["due_now"]), ("progress.bin", 3, 1))
        self.assertEqual(r["entries"][0]["prompt"], prompt)
        self.assertEqual(r["entries"][1]["box"], 1)  # out-of-range box reads as box 1
        self.assertIsNone(r["entries"][1]["prompt"])
        self.assertEqual(r["boxes"], {"1": 1, "2": 1, "3": 0, "4": 0, "5": 0})
        r = json.loads(run(["progress", a, b, "--pack", str(self.pack)])[1])
        self.assertEqual((r["from"], r["generation"]), ("progress.tmp", 4))
        r = json.loads(run(["progress", b, a, "--pack", str(self.pack)])[1])
        self.assertEqual((r["from"], r["generation"]), ("progress.tmp", 4))  # named by file
        r = json.loads(run(["progress", a, b, "--pack", "/nonexistent.pkp"])[1])
        self.assertIsNone(r["entries"][0]["prompt"])

    def test_progress_rejects_what_the_app_rejects(self):
        bad = [b"", b"XXXX" + bytes(20), progress_image([], version=2),
               progress_image([(1, 0, 1, 0, 1, 1)], crc_ok=False),
               progress_image([(1, 0, 1, 0, 1, 1)])[:-6],
               b"PKPS" + struct.pack("<HHIHHI", 1, 0, 1, 1, 300, 0) + bytes(4)]
        for img in bad:
            self.assertIsNone(bridge.decode_progress_image(img))
        f = self.write("bad.bin", bad[-1])
        rc, out, _ = run(["progress", f, str(Path(self.tmp.name) / "missing.tmp")])
        self.assertEqual(rc, 1)
        self.assertEqual(json.loads(out), {"valid": False, "entries": []})


class EntryPointTest(unittest.TestCase):
    def test_help_runs_as_a_program(self):
        import runpy

        with mock.patch.object(sys, "argv", ["bridge.py", "--help"]):
            with self.assertRaises(SystemExit) as cm:
                with redirect_stdout(io.StringIO()):
                    runpy.run_path(str(ROOT / "tools" / "bridge.py"), None, "__main__")
        self.assertEqual(cm.exception.code, 0)


if __name__ == "__main__":
    unittest.main()
