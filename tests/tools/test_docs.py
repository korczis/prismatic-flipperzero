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
        self.patches = [mock.patch.object(docs, "ROOT", root),
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
        self.assertIn("first | with bar", docs.timeline())
        self.assertEqual(docs.fmt(1234567), "1 234 567")

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
        with mock.patch.object(sys, "argv", ["docs.py", "--check"]):
            with self.assertRaises(SystemExit) as cm:
                quiet(runpy.run_path, str(ROOT / "tools" / "docs.py"), None, "__main__")
        self.assertEqual(cm.exception.code, 0)


if __name__ == "__main__":
    unittest.main()
