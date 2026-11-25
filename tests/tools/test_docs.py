"""Tests for tools/docs.py: derived Markdown regions, drift detection and snapshots."""

import io
import json
import runpy
import subprocess
import sys
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

import docs  # noqa: E402

REPORT = {
    "pack": "t.pkp", "bytes": 1234, "sha256": "ab" * 32, "objects": 3, "categories": 2,
    "relations": 5, "capabilities": {"detect": 1, "apply": 2, "compare": 1, "browse": 3, "quiz": 1},
    "quiz": {"definition": 2, "trainer": 1}, "kinds": {"technique": 2, "concept": 1},
    "items": {"indicators": 4, "actions": 3, "escalation": 1, "heading": 6},
}


def quiet(fn, *a):
    out, err = io.StringIO(), io.StringIO()
    with redirect_stdout(out), redirect_stderr(err):
        rc = fn(*a)
    return rc, out.getvalue(), err.getvalue()


class DocsTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        root = Path(self.tmp.name)
        (root / "docs" / "evidence" / "pocket").mkdir(parents=True)
        (root / "docs" / "data").mkdir()
        (root / "docs" / "data" / "pack-stats.json").write_text(json.dumps(REPORT))
        (root / "docs" / "evidence" / "pocket" / "manifest.json").write_text(json.dumps({
            "captured": "2026-11-25", "firmware": "1.5.1", "command": "c",
            "frames": [{"file": "a.png", "caption": "A"}, {"file": "b.png", "caption": "B"},
                       {"file": "c.png", "caption": "C"}]}))
        subprocess.run(["git", "init", "-q", str(root)], check=True)
        subprocess.run(["git", "-C", str(root), "-c", "user.email=t@t", "-c", "user.name=t",
                        "commit", "-q", "--allow-empty", "-m", "first | with bar"], check=True)
        self.root = root
        (root / "docs" / "evidence" / "atlas").mkdir(parents=True)
        (root / "docs" / "evidence" / "atlas" / "manifest.json").write_text(json.dumps({
            "command": "tools/atlas.py", "spec": "tests/sim/atlas.txt",
            "frames": [{"file": "01-home.png", "caption": "Home"}]}))
        (root / "docs" / "evidence" / "cache").mkdir(parents=True)
        (root / "docs" / "evidence" / "cache" / "bench.json").write_text(json.dumps({
            "command": "c.sh", "walk": "tour", "steps": 3,
            "cache": {"blocks": 8, "block_bytes": 256},
            "cached": {"card_reads": 40, "bytes": 1100, "hits": 60, "misses": 40},
            "direct": {"card_reads": 100, "bytes": 1000, "hits": 0, "misses": 0}}))
        (root / "app" / "ui").mkdir(parents=True)
        (root / "app" / "ui" / "ui_app.c").write_text(
            'static const Hub hubs[] = {\n    {UiIconBrowse,\n     "Library",\n     HomeAll,\n'
            '     "%lu objects",\n     {{UiIconFolder, HomeBrowse, "By chapter", "%lu", "Chapters",'
            ' ShareNone},\n      {UiIconTrainer,\n       HomeQuizExample,\n       "Manip?",\n'
            '       "%lu",\n       "Benign or not",\n       ShareQuestions}}},\n};\n'
            '#define HUB_COUNT 1\n'
            'page_add_tile(app->page, UiIconInfo, "Info", "device", T_HOME | HomeInfo);\n')
        self.patches = [mock.patch.object(docs, "ROOT", root),
                        mock.patch.object(docs, "ATLAS",
                                          root / "docs/evidence/atlas/manifest.json"),
                        mock.patch.object(docs, "CACHE", root / "docs/evidence/cache/bench.json"),
                        mock.patch.object(docs, "UI_APP", root / "app/ui/ui_app.c"),
                        mock.patch.object(docs, "STATS", root / "docs/data/pack-stats.json"),
                        mock.patch.object(docs, "SCREENS",
                                          root / "docs/evidence/pocket/manifest.json")]
        for p in self.patches:
            p.start()

    def tearDown(self):
        for p in self.patches:
            p.stop()
        self.tmp.cleanup()

    def write(self, name, regions):
        body = "\n".join(f"<!-- docs:begin {r} -->\nstale\n<!-- docs:end {r} -->" for r in regions)
        path = self.root / name
        path.write_text(f"# Doc\n\n{body}\n")
        return path

    def test_generators(self):
        stats = docs.pack_stats()
        self.assertIn("| Knowledge objects | **3** |", stats)
        self.assertIn("| Quiz questions | **3** |", stats)
        kinds = docs.pack_kinds()
        self.assertLess(kinds.index("technique"), kinds.index("concept"))
        self.assertIn("66.7 %", kinds)
        gallery = docs.screens()
        self.assertEqual(gallery.count("<tr>"), 2)  # three frames, two per row
        self.assertIn("firmware 1.5.1", gallery)
        frames = docs.atlas()
        self.assertIn("docs/evidence/atlas/01-home.png", frames)
        self.assertIn("Rendered by `tools/atlas.py` from `tests/sim/atlas.txt`", frames)
        bench = docs.cache_bench()
        self.assertIn("| Reading the card directly | 100 | 1 000 |", bench)
        self.assertIn("60 of 100 block requests from memory: 60% fewer card reads for +10%", bench)
        tree = docs.ia_tree()
        self.assertIn('home --> h0["Library"]', tree)
        self.assertIn('h0 --> h0e1["Manip?<br/><small>Benign or not</small>"]', tree)
        self.assertIn('home --> t0["Info"]', tree)
        self.assertIn("first | with bar", docs.timeline())
        self.assertEqual(docs.fmt(1234567), "1 234 567")
        geometry = docs.ui_geometry()  # the real engine source
        self.assertIn("| `LEADING` | 11 px | body line pitch |", geometry)
        self.assertIn("| `TEXT_CAP` | 12288 | text per page |", geometry)

    def test_write_then_check(self):
        readme = self.write("README.md", ["pack-stats", "screens"])
        self.write("docs/x.md", ["pack-kinds", "timeline"])
        rc, out, _ = quiet(docs.main, ["--check"])
        self.assertEqual(rc, 1)
        rc, out, _ = quiet(docs.main, [])
        self.assertEqual(rc, 0)
        self.assertIn("updated README.md", out)
        self.assertNotIn("stale", readme.read_text())
        self.assertEqual(quiet(docs.main, ["--check"])[0], 0)
        rc, out, err = quiet(docs.main, [])  # nothing left to update
        self.assertEqual((rc, out), (0, ""))

    def test_check_names_the_drifted_region(self):
        self.write("README.md", ["pack-stats"])
        rc, _, err = quiet(docs.main, ["--check"])
        self.assertEqual(rc, 1)
        self.assertIn("README.md: pack-stats", err)

    def test_timeline_may_lag_but_not_lie(self):
        doc = self.write("README.md", ["timeline"])
        quiet(docs.main, [])
        subprocess.run(["git", "-C", str(self.root), "-c", "user.email=t@t", "-c", "user.name=t",
                        "commit", "-q", "--allow-empty", "-m", "second"], check=True)
        self.assertEqual(quiet(docs.main, ["--check"])[0], 0)  # one commit behind is fine
        doc.write_text(doc.read_text().replace("first | with bar", "invented"))
        self.assertEqual(quiet(docs.main, ["--check"])[0], 1)

    def test_unknown_region_is_refused(self):
        self.write("README.md", ["nonsense"])
        with self.assertRaises(SystemExit):
            quiet(docs.main, [])

    def test_snapshot(self):
        report = self.root / "r.json"
        report.write_text(json.dumps({"objects": 9}))
        rc, out, _ = quiet(docs.main, ["--snapshot", str(report)])
        self.assertEqual(rc, 0)
        self.assertEqual(json.loads((self.root / "docs/data/pack-stats.json").read_text()),
                         {"objects": 9})

    def test_entry_point(self):
        self.write("README.md", [])
        with mock.patch.object(sys, "argv", ["docs.py", "--check"]), \
                mock.patch.dict("os.environ", {"POCKET_ROOT": str(self.root)}):
            with self.assertRaises(SystemExit) as cm:
                quiet(runpy.run_path, str(ROOT / "tools" / "docs.py"), None, "__main__")
        self.assertEqual(cm.exception.code, 0)


if __name__ == "__main__":
    unittest.main()
