"""Tests for tools/wiki.py: wiki pages are the repository's Markdown with links rewritten."""

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

import wiki  # noqa: E402


class WikiTest(unittest.TestCase):
    def test_rewrite(self):
        text = ("<!-- docs:begin x -->\n[a](docs/case-study.md#t) [b](tools/x.py) "
                '<img src="docs/a.png"> [c](https://e.org) [d](#here) [e](docs/x.svg)\n'
                "<!-- docs:end x -->\n")
        out = wiki.rewrite(text, "README.md")
        self.assertIn("[a](Case-Study#t)", out)
        self.assertIn(f"[b]({wiki.REPO}/blob/main/tools/x.py)", out)
        self.assertIn(f'src="{wiki.REPO}/raw/main/docs/a.png"', out)
        self.assertIn(f"[e]({wiki.REPO}/raw/main/docs/x.svg)", out)
        self.assertIn("[c](https://e.org) [d](#here)", out)
        self.assertNotIn("docs:begin", out)
        up = wiki.rewrite("[r](../README.md) [t](./architecture.md) [u](../tools/)",
                          "docs/case-study.md")
        self.assertEqual(up, f"[r](Home) [t](Architecture) [u]({wiki.REPO}/blob/main/tools)")

    def test_render_and_cli(self):
        with tempfile.TemporaryDirectory() as src, tempfile.TemporaryDirectory() as d, \
                mock.patch.object(wiki, "ROOT", Path(src)):
            for _, page in wiki.PAGES:
                (Path(src) / page).parent.mkdir(parents=True, exist_ok=True)
                (Path(src) / page).write_text(f"# {page}\n\n[x](docs/case-study.md)\n")
            out = io.StringIO()
            with redirect_stdout(out):
                self.assertEqual(wiki.main([d]), 0)
            self.assertIn("Home", out.getvalue())
            home = (Path(d) / "Home.md").read_text()
            self.assertIn("Generated from [`README.md`]", home)
            self.assertIn("(Case-Study)", (Path(d) / "_Sidebar.md").read_text())
            with redirect_stderr(io.StringIO()):
                self.assertEqual(wiki.main([]), 2)
            with mock.patch.object(sys, "argv", ["wiki.py", d]), \
                    mock.patch.dict("os.environ", {"POCKET_ROOT": src}):
                with self.assertRaises(SystemExit) as cm:
                    with redirect_stdout(io.StringIO()):
                        runpy.run_path(str(ROOT / "tools" / "wiki.py"), run_name="__main__")
            self.assertEqual(cm.exception.code, 0)


if __name__ == "__main__":
    unittest.main()
