"""Tests for tools/*.py. Run: python3 -m unittest discover -s tests/tools"""

import io
import json
import os
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

import flipper_screen as fs  # noqa: E402
import icons  # noqa: E402
import sdk  # noqa: E402
import version  # noqa: E402


def png_size(data):
    assert data[:8] == b"\x89PNG\r\n\x1a\n"
    return int.from_bytes(data[16:20], "big"), int.from_bytes(data[20:24], "big")


def quiet(fn, *a):
    out, err = io.StringIO(), io.StringIO()
    with redirect_stdout(out), redirect_stderr(err):
        rc = fn(*a)
    return rc, out.getvalue(), err.getvalue()


class IconsTest(unittest.TestCase):
    def test_app_icon_is_10x10_and_logo_40x40(self):
        self.assertEqual(png_size(icons.png(icons.APP_ICON)), (10, 10))
        rows = icons.logo(40)
        self.assertEqual((len(rows[0]), len(rows)), (40, 40))
        self.assertGreater(sum(r.count("#") for r in rows), 100)

    def test_png_bits_round_trip(self):
        rows = ["#.#.#.#.#", ".#.#.#.#."]
        data = icons.png(rows)
        idat = data[data.index(b"IDAT") + 4 :]
        raw = zlib.decompress(idat[: idat.index(b"IEND") - 8])
        self.assertEqual(raw[0], 0)
        self.assertEqual(raw[1], 0b01010101)  # '#' is black (0)
        self.assertEqual(raw[2] >> 7, 0)

    def test_committed_icons_are_in_sync(self):
        self.assertEqual(icons.main(["--check"]), 0)

    def test_check_reports_drift_and_write_repairs_it(self):
        with tempfile.TemporaryDirectory() as d:
            target = Path(d) / "sub" / "x.png"
            with mock.patch.object(icons, "TARGETS", {target: icons.APP_ICON}), \
                    mock.patch.object(icons, "ROOT", Path(d)):
                rc, _, err = quiet(icons.main, ["--check"])
                self.assertEqual(rc, 1)
                self.assertIn("sub/x.png", err)
                self.assertEqual(icons.main([]), 0)
                self.assertEqual(icons.main(["--check"]), 0)
                rc, out, _ = quiet(icons.main, ["--preview"])
                self.assertEqual(rc, 0)
                self.assertIn("....#.....", out)


class VersionTest(unittest.TestCase):
    def tree(self, d, version_text, fam_version):
        root = Path(d)
        (root / "app").mkdir()
        (root / "VERSION").write_text(version_text)
        (root / "app" / "application.fam").write_text(f'App(fap_version="{fam_version}")\n')
        return root

    def test_repository_is_in_sync(self):
        self.assertEqual(version.main(["--check"]), 0)

    def test_generate_then_check(self):
        with tempfile.TemporaryDirectory() as d:
            root = self.tree(d, "1.2.3\n", "1.2")
            with mock.patch.object(version, "ROOT", root):
                rc, _, err = quiet(version.main, ["--check"])
                self.assertEqual(rc, 1)
                self.assertIn("drifted", err)
                self.assertEqual(version.main([]), 0)
                self.assertIn('"1.2.3"', (root / "app" / "version.h").read_text())
                self.assertEqual(version.main(["--check"]), 0)

    def test_fam_major_minor_mismatch_fails(self):
        with tempfile.TemporaryDirectory() as d:
            root = self.tree(d, "1.2.3\n", "1.3")
            with mock.patch.object(version, "ROOT", root):
                rc, _, err = quiet(version.main, [])
                self.assertEqual(rc, 1)
                self.assertIn('fap_version must be "1.2"', err)

    def test_malformed_version_fails(self):
        with tempfile.TemporaryDirectory() as d:
            root = self.tree(d, "1.2\n", "1.2")
            with mock.patch.object(version, "ROOT", root):
                rc, _, err = quiet(version.main, ["--check"])
                self.assertEqual(rc, 1)
                self.assertIn("MAJOR.MINOR.PATCH", err)


class SdkTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.home = Path(self.tmp.name) / "ufbt-home"
        self.zip_bytes = b"PK fake sdk"
        import hashlib

        self.entry = {
            "id": "fake", "label": "Fake", "version": "9.9", "status": "compat",
            "url": "https://example.invalid/sdk/fake-sdk.zip",
            "sha256": hashlib.sha256(self.zip_bytes).hexdigest(),
            "ufbt_home": str(self.home),
        }
        self.patch = mock.patch.object(sdk, "MATRIX", [self.entry])
        self.patch.start()

    def tearDown(self):
        self.patch.stop()
        self.tmp.cleanup()

    def test_matrix_file_is_well_formed(self):
        matrix = json.loads((ROOT / "tools" / "sdks.json").read_text())["sdks"]
        self.assertEqual({e["id"] for e in matrix}, {"official", "momentum"})
        for e in matrix:
            self.assertRegex(e["sha256"], r"^[0-9a-f]{64}$")
            self.assertTrue(e["url"].startswith("https://"))

    def test_list_env_and_unknown(self):
        rc, out, _ = quiet(sdk.main, ["list"])
        self.assertEqual((rc, out), (0, "fake\t9.9\tcompat\n"))
        rc, out, _ = quiet(sdk.main, ["env", "fake"])
        self.assertEqual(out.strip(), f"UFBT_HOME={self.home}")
        with self.assertRaises(SystemExit):
            sdk.entry("nope")
        rc, _, err = quiet(sdk.main, [])
        self.assertEqual(rc, 2)

    def test_check_false_until_installed(self):
        rc, out, _ = quiet(sdk.main, ["check", "fake"])
        self.assertEqual(rc, 1)
        self.assertIn("not installed", out)

    def fake_urlopen(self, payload):
        class Resp(io.BytesIO):
            def __enter__(self):
                return self

            def __exit__(self, *a):
                return False

        return lambda req, timeout: Resp(payload)

    def test_install_verifies_checksum_and_marks_home(self):
        shared = Path(self.tmp.name) / "shared-toolchain"
        shared.mkdir()

        def fake_run(cmd, env, check):
            self.assertEqual(cmd[:4], ["ufbt", "update", "--hw-target", "f7"])
            self.assertEqual(env["UFBT_HOME"], str(self.home))
            (self.home / "current").mkdir(parents=True, exist_ok=True)
            (self.home / "current" / "ufbt_state.json").write_text("{}")

        real_expand = os.path.expanduser
        with mock.patch.object(sdk.urllib.request, "urlopen", self.fake_urlopen(self.zip_bytes)), \
                mock.patch.object(sdk.subprocess, "run", fake_run), \
                mock.patch.object(sdk.os.path, "expanduser",
                                  lambda p: str(shared) if p == "~/.ufbt/toolchain"
                                  else real_expand(p)):
            rc, out, _ = quiet(sdk.main, ["install", "fake"])
            self.assertEqual(rc, 0)
            self.assertTrue((self.home / "toolchain").is_symlink())
            rc, _, _ = quiet(sdk.main, ["check", "fake"])
            self.assertEqual(rc, 0)
            # a second install reuses the verified zip without downloading
            with mock.patch.object(sdk.urllib.request, "urlopen", side_effect=AssertionError):
                rc, _, _ = quiet(sdk.main, ["install", "fake"])
            self.assertEqual(rc, 0)

    def test_install_refuses_a_wrong_checksum(self):
        with mock.patch.object(sdk.urllib.request, "urlopen", self.fake_urlopen(b"tampered")):
            with self.assertRaises(SystemExit) as cm:
                quiet(sdk.main, ["install", "fake"])
        self.assertIn("SHA-256 mismatch", str(cm.exception))
        self.assertFalse((self.home / "download" / "fake-sdk.zip").exists())


def frame_bytes(lit):
    """A 1024-byte ScreenFrame with the pixels in `lit` set."""
    data = bytearray(1024)
    for x, y in lit:
        data[x + (y // 8) * 128] |= 1 << (y % 8)
    return bytes(data)


class FakeFlipper:
    """Speaks just enough CLI and RPC: prompt, start_rpc_session, screen stream, input."""

    def __init__(self, port, baud, timeout):
        self.port, self.out, self.rx, self.rpc = port, bytearray(), b"", False
        self.inputs, self.frames_sent, self.closed = [], 0, False

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

    def _frame(self):
        self.frames_sent += 1
        body = fs.field(22, 2, fs.field(1, 2, frame_bytes([(self.frames_sent, 0)])))
        self.out += fs.varint(len(body)) + body

    def write(self, data):
        if not self.rpc:
            if data in (b"\r", b"\x03\r"):
                self.out += b"\r\n>: "
            elif data == b"loader info\r":
                self.out += b"loader info\r\nNo application is running\r\n\r\n>: "
            elif data == b"hang\r":
                pass
            elif data == b"start_rpc_session\r":
                self.out += b"start_rpc_session\r\n"
                self.rpc = True
            return
        self.rx += data
        while self.rx:
            ln, i = fs.read_varint(self.rx, 0)
            msg, self.rx = self.rx[i: i + ln], self.rx[i + ln:]
            fields = {n: v for n, _w, v in fs.parse_fields(msg)}
            if 20 in fields:
                self._frame()
            if 23 in fields:
                ev = {n: v for n, _w, v in fs.parse_fields(fields[23])}
                self.inputs.append((ev.get(1, 0), ev.get(2, 0)))
                if ev.get(2) == fs.RELEASE:
                    self._frame()


class FlipperScreenTest(unittest.TestCase):
    def test_varint_and_fields_round_trip(self):
        for n in (0, 1, 127, 128, 300, 2**32 - 1):
            self.assertEqual(fs.read_varint(fs.varint(n), 0), (n, len(fs.varint(n))))
        msg = fs.field(1, 0, 7) + fs.field(2, 2, b"ab") + b"\x1d" + b"wxyz" + b"\x21" + b"8bytes!!"
        self.assertEqual(list(fs.parse_fields(msg)),
                         [(1, 0, 7), (2, 2, b"ab"), (3, 5, b"wxyz"), (4, 1, b"8bytes!!")])
        with self.assertRaises(ValueError):
            list(fs.parse_fields(b"\x0b"))
        with self.assertRaises(IndexError):
            fs.read_varint(b"\x80", 0)
        m = fs.main_msg(5, 20, b"")
        ln, i = fs.read_varint(m, 0)
        self.assertEqual(dict((n, v) for n, _w, v in fs.parse_fields(m[i:])), {1: 5, 20: b""})

    def test_frame_layout_and_png(self):
        rows = fs.frame_to_rows(frame_bytes([(0, 0), (127, 63), (5, 9)]))
        self.assertTrue(rows[0][0] and rows[63][127] and rows[9][5])
        self.assertFalse(rows[0][1])
        self.assertEqual(png_size(fs.png(rows, 2)), (256, 128))
        with self.assertRaises(ValueError):
            fs.frame_to_rows(b"\x00" * 10)

    def run_main(self, argv):
        fake_mod = types.SimpleNamespace(Serial=FakeFlipper)
        with mock.patch.dict(sys.modules, {"serial": fake_mod}):
            return quiet(fs.main, argv)

    def test_capture_writes_a_genuine_frame(self):
        with tempfile.TemporaryDirectory() as d:
            out = Path(d) / "s.png"
            rc, stdout, _ = self.run_main(["capture", str(out), "--port", "/dev/fake", "--scale", "1"])
            self.assertEqual(rc, 0)
            self.assertEqual(png_size(out.read_bytes()), (128, 64))

    def test_walk_presses_keys_and_captures_each_frame(self):
        with tempfile.TemporaryDirectory() as d:
            rc, stdout, _ = self.run_main(["walk", d, "right,ok:long,back", "--port",
                                           "/dev/fake"])
            self.assertEqual(rc, 0)
            self.assertEqual(sorted(p.name for p in Path(d).iterdir()),
                             ["00-start.png", "01-right.png", "02-ok-long.png", "03-back.png"])

    def test_walk_rejects_unknown_key(self):
        with tempfile.TemporaryDirectory() as d:
            with self.assertRaises(SystemExit):
                self.run_main(["walk", d, "sideways", "--port", "/dev/fake"])

    def test_cli_timeout_is_reported(self):
        class Silent(FakeFlipper):
            def write(self, data):
                pass

        with mock.patch.dict(sys.modules, {"serial": types.SimpleNamespace(Serial=Silent)}):
            with mock.patch.object(fs.time, "time", side_effect=[0, 0, 99]):
                with self.assertRaises(TimeoutError):
                    fs.Rpc("/dev/fake")

    def test_frame_and_message_timeouts(self):
        with mock.patch.dict(sys.modules, {"serial": types.SimpleNamespace(Serial=FakeFlipper)}):
            rpc = fs.Rpc("/dev/fake")
        with self.assertRaises(TimeoutError):
            rpc.next_message(timeout=0.05)
        with self.assertRaises(TimeoutError):
            rpc.frame(timeout=0.1, settle=0.02)
        # a non-frame message before the frame is skipped
        body = fs.field(1, 0, 1)
        rpc.s.out += fs.varint(len(body)) + body
        rpc.send(20)
        self.assertEqual(len(rpc.frame(timeout=1.0, settle=0.1)), 1024)
        rpc.close()
        self.assertTrue(rpc.s.closed)

    def test_cli_runs_one_command(self):
        rc, out, _ = self.run_main(["cli", "loader info", "--port", "/dev/fake"])
        self.assertEqual(rc, 0)
        self.assertEqual(out.strip(), "No application is running")
        with mock.patch.dict(sys.modules, {"serial": types.SimpleNamespace(Serial=FakeFlipper)}):
            with self.assertRaises(TimeoutError):
                fs.cli("/dev/fake", "hang", timeout=0.3)
        with mock.patch.object(fs, "find_port", lambda: "/dev/fake"):
            rc, out, _ = self.run_main(["cli", "loader info"])
        self.assertEqual(rc, 0)

    def test_find_port(self):
        with mock.patch.object(fs.glob, "glob", lambda pat: ["/dev/cu.usbmodemflip_X1"]
                               if "usbmodemflip" in pat else []):
            self.assertEqual(fs.find_port(), "/dev/cu.usbmodemflip_X1")
        with mock.patch.object(fs.glob, "glob", lambda pat: []):
            with self.assertRaises(SystemExit):
                fs.find_port()
        with mock.patch.object(fs, "find_port", lambda: "/dev/fake"):
            with tempfile.TemporaryDirectory() as d:
                rc, _, _ = self.run_main(["capture", str(Path(d) / "a.png")])
                self.assertEqual(rc, 0)

    def test_continuous_stream_returns_newest_frame_at_timeout(self):
        with mock.patch.dict(sys.modules, {"serial": types.SimpleNamespace(Serial=FakeFlipper)}):
            rpc = fs.Rpc("/dev/fake")
        frame_msg = {22: fs.field(1, 2, frame_bytes([(9, 9)]))}
        clock = __import__("itertools").count(0.0, 0.3)
        with mock.patch.object(fs.time, "time", lambda: next(clock)), \
                mock.patch.object(rpc, "next_message", lambda timeout: frame_msg):
            self.assertEqual(rpc.frame(timeout=1.0, settle=5.0), frame_bytes([(9, 9)]))


class EntryPointTest(unittest.TestCase):
    """Each tool runs as a program; its __main__ guard maps main()'s result to the exit code."""

    def run_tool(self, name, argv):
        import runpy

        with mock.patch.object(sys, "argv", [name] + argv):
            with self.assertRaises(SystemExit) as cm:
                quiet(runpy.run_path, str(ROOT / "tools" / name), None, "__main__")
        return cm.exception.code

    def test_entry_points(self):
        self.assertEqual(self.run_tool("icons.py", ["--check"]), 0)
        self.assertEqual(self.run_tool("version.py", ["--check"]), 0)
        self.assertEqual(self.run_tool("sdk.py", ["list"]), 0)
        self.assertEqual(self.run_tool("flipper_screen.py", ["--help"]), 0)


if __name__ == "__main__":
    unittest.main()
