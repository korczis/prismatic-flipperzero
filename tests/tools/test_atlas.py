"""Tests for tools/atlas.py with a fake simulator: walks, screen coverage, staleness."""

import io
import json
import os
import runpy
import sys
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

import atlas  # noqa: E402

UI_APP = "typedef enum {\n    ScrHome,\n    ScrHub,\n} Screen;\n"
SPEC = ("# comment\n\n"
        "home | kompendium | | Home | The home grid\n"
        "menu | ui | ok:long | Home+menu | A menu\n")


def fake_run(screens):
    """subprocess.run stand-in: the simulator writes one frame and prints the given screen."""
    def run(cmd, **kw):
        if Path(cmd[0]).name == "pocketsim":
            Path(cmd[2], "00-start.bin").write_bytes(bytes(1024))
            name = Path(cmd[2]).name
            overlay = "[Actions]\n" if name == "menu" else ""
            screen = screens.get(name, "Home")
            return SimpleNamespace(stdout=f"== 00 start\n@ {screen}\n# x\n{overlay}")
        return SimpleNamespace(stdout="")
    return run


class AtlasTest(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        root = Path(self.dir.name)
        (root / "ui_app.c").write_text(UI_APP)
        (root / "atlas.txt").write_text(SPEC)
        self.out = root / "docs" / "atlas"
        self.patches = [mock.patch.object(atlas, "ROOT", root),
                        mock.patch.object(atlas, "UI_APP", root / "ui_app.c"),
                        mock.patch.object(atlas, "SPEC", root / "atlas.txt"),
                        mock.patch.object(atlas, "OUT", self.out),
                        mock.patch.object(atlas, "SIM", root / "pocketsim")]
        for p in self.patches:
            p.start()

    def tearDown(self):
        for p in self.patches:
            p.stop()
        self.dir.cleanup()

    def test_parse_and_views(self):
        self.assertEqual(atlas.screens(), ["Home", "Hub"])
        self.assertEqual([e["view"] for e in atlas.parse(SPEC)], ["Home", "Home+menu"])
        self.assertEqual(atlas.last_view("== 00\n@ Map\n== 01\n@ Article\n[Contents]"),
                         "Article+contents")
        self.assertEqual(atlas.last_view("== 00\n@ Map\n x"), "Map")

    def test_pixels_ignore_compression(self):
        import zlib
        raw = b"\x00" + b"\xff" * 4
        def png(level):
            body = zlib.compress(raw, level)
            ihdr = b"IHDR" + (4).to_bytes(4, "big") + (1).to_bytes(4, "big") + b"\x08\x00\x00\x00\x00"
            chunk = lambda kind, d: len(d).to_bytes(4, "big") + kind + d + b"\0\0\0\0"
            return (b"\x89PNG\r\n\x1a\n" + len(ihdr[4:]).to_bytes(4, "big") + ihdr + b"\0\0\0\0"
                    + chunk(b"IDAT", body) + chunk(b"IEND", b""))
        self.assertNotEqual(png(1), png(9))
        self.assertEqual(atlas.pixels(png(1)), atlas.pixels(png(9)))
        self.assertEqual(atlas.pixels(b"{}"), b"{}")

    def test_render_and_check(self):
        good = fake_run({"home": "Home", "menu": "Home"})
        with mock.patch.object(atlas.subprocess, "run", good):
            with redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()) as err:
                self.assertEqual(atlas.main([]), 1)  # Hub has no frame
            self.assertIn("screen Hub has no frame", err.getvalue())
            manifest = json.loads((self.out / "manifest.json").read_text())
            self.assertEqual([f["file"] for f in manifest["frames"]],
                             ["01-home.png", "02-menu.png"])
            self.assertTrue((self.out / "sheet.png").exists())
            with redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()) as err:
                self.assertEqual(atlas.main([]), 1)  # rendering again replaces the folder
            (Path(self.dir.name) / "atlas.txt").write_text(
                SPEC + "hub | kompendium | ok | Hub | A hub\n")
        wrong = fake_run({"home": "Home", "menu": "Home", "hub": "Home"})
        with mock.patch.object(atlas.subprocess, "run", wrong):
            with redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()) as err:
                self.assertEqual(atlas.main(["--check"]), 1)
            self.assertIn("hub: ends on Home, declares Hub", err.getvalue())
            self.assertIn("03-hub.png is stale", err.getvalue())
        right = fake_run({"home": "Home", "menu": "Home", "hub": "Hub"})
        with mock.patch.object(atlas.subprocess, "run", right):
            with redirect_stdout(io.StringIO()):
                self.assertEqual(atlas.main([]), 0)
            with redirect_stdout(io.StringIO()) as out:
                self.assertEqual(atlas.main(["--check"]), 0)
            self.assertIn("3 frames current, every screen covered", out.getvalue())

    def test_entry_point(self):
        right = fake_run({"home": "Home", "menu": "Home"})
        (Path(self.dir.name) / "ui_app.c").write_text("typedef enum {\n    ScrHome,\n} Screen;\n")
        (Path(self.dir.name) / "build" / "packs").mkdir(parents=True)
        (Path(self.dir.name) / "build" / "packs" / "kompendium.pkp").write_bytes(b"")
        with mock.patch.object(atlas.subprocess, "run", right):
            with redirect_stdout(io.StringIO()):
                self.assertEqual(atlas.main([]), 0)
        # the entry point, hermetic: POCKET_ROOT points the tool at a scratch tree
        tree = Path(self.dir.name) / "tree"
        (tree / "tests" / "sim").mkdir(parents=True)
        (tree / "tests" / "sim" / "atlas.txt").write_text(SPEC)
        (tree / "app" / "ui").mkdir(parents=True)
        (tree / "app" / "ui" / "ui_app.c").write_text(UI_APP)
        argv = ["atlas.py", "--check"]
        env = {"POCKET_ROOT": str(tree)}
        with mock.patch("subprocess.run", right), mock.patch.object(sys, "argv", argv), \
                mock.patch.dict(os.environ, env):
            with self.assertRaises(SystemExit) as cm:
                with redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
                    runpy.run_path(str(ROOT / "tools" / "atlas.py"), run_name="__main__")
        self.assertEqual(cm.exception.code, 1)  # nothing rendered there yet: every frame stale


if __name__ == "__main__":
    unittest.main()
