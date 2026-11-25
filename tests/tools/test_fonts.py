"""Tests for tools/fonts.py: font arrays are copied from the u8g2 source byte for byte."""

import io
import runpy
import sys
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

import fonts  # noqa: E402

SOURCE = ('const uint8_t u8g2_font_other_tr[3] U8G2_FONT_SECTION("u8g2_font_other_tr") =\n'
          '  "ab";\n'
          'const uint8_t u8g2_font_helvR08_tr[5] U8G2_FONT_SECTION("u8g2_font_helvR08_tr") =\n'
          '  "\\0\\3"\n  "x\\"y";\n')


class FontsTest(unittest.TestCase):
    def test_extract_and_render(self):
        size, body = fonts.extract(SOURCE, "u8g2_font_helvR08_tr")
        self.assertEqual(size, "5")
        self.assertEqual(body, '"\\0\\3"\n  "x\\"y"')
        text = fonts.render(SOURCE)
        self.assertIn('const uint8_t pocket_font_body[5] = "\\0\\3"', text)
        self.assertIn("clang-format off", text)
        with self.assertRaises(SystemExit):
            fonts.extract(SOURCE, "u8g2_font_missing_tr")

    def test_write_check_and_entry_point(self):
        with tempfile.TemporaryDirectory() as d:
            src, out = Path(d) / "u8g2_fonts.c", Path(d) / "app" / "ui" / "fonts.c"
            out.parent.mkdir(parents=True)
            src.write_text(SOURCE)
            args = ["--u8g2", str(src)]
            with mock.patch.object(fonts, "OUT", out), mock.patch.object(fonts, "ROOT", Path(d)):
                with redirect_stderr(io.StringIO()):
                    self.assertEqual(fonts.main(["--check", *args]), 1)  # missing
                with redirect_stdout(io.StringIO()):
                    self.assertEqual(fonts.main(args), 0)
                self.assertEqual(fonts.main(["--check", *args]), 0)
                out.write_text(out.read_text() + "/* edited */\n")
                with redirect_stderr(io.StringIO()):
                    self.assertEqual(fonts.main(["--check", *args]), 1)  # drift
                # without --u8g2 the source is the simulator's fetched u8g2
                default = Path(d) / "build" / "sim" / "u8g2" / "u8g2_fonts.c"
                default.parent.mkdir(parents=True)
                default.write_text(SOURCE)
                with redirect_stdout(io.StringIO()):
                    self.assertEqual(fonts.main([]), 0)
            with mock.patch.object(sys, "argv", ["fonts.py", "--check", *args]):
                with self.assertRaises(SystemExit) as cm:
                    with redirect_stderr(io.StringIO()):
                        runpy.run_path(str(ROOT / "tools" / "fonts.py"), run_name="__main__")
            self.assertEqual(cm.exception.code, 1)  # the real app/ui/fonts.c is not this fixture


if __name__ == "__main__":
    unittest.main()
