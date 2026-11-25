"""Tests for tools/explorer.py: explorer data matches the device object order and quizzes."""

import io
import json
import runpy
import sys
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "tests" / "tools"))

import pocket  # noqa: E402
import explorer  # noqa: E402
from test_pocket import tiny_ir  # noqa: E402


class SiteTest(unittest.TestCase):
    def test_build_matches_pack_order_and_quiz(self):
        ir = tiny_ir()
        with tempfile.TemporaryDirectory() as d:
            out = Path(d)
            summary = explorer.build(ir, out)
            data = json.loads((out / "index.json").read_text())
            quiz = json.loads((out / "quiz.json").read_text())
            dump = pocket.decode(pocket.encode(ir))
            self.assertEqual([r["t"] for r in data["objects"]],
                             [pocket.fold(o["title"]) for o in dump["objects"]])
            self.assertEqual(len(quiz), len(dump["quiz"]))
            self.assertEqual(summary["objects"], 5)
            self.assertEqual([c["count"] for c in data["categories"]], [3, 2])
            first = json.loads((out / "o" / "0.json").read_text())
            self.assertEqual(first["i"], 0)
            self.assertTrue(all(isinstance(r["to"], int) for r in first["relations"]))
            self.assertEqual(sum(data["kindCounts"].values()), 5)

    def test_cli(self):
        with tempfile.TemporaryDirectory() as d:
            ir_path = Path(d) / "ir.json"
            ir_path.write_text(json.dumps(tiny_ir()))
            out = io.StringIO()
            with redirect_stdout(out):
                self.assertEqual(explorer.main(["--ir", str(ir_path), "--out", str(Path(d) / "s")]),
                                 0)
            self.assertEqual(json.loads(out.getvalue())["objects"], 5)
            with mock.patch.object(sys, "argv", ["explorer.py", "--ir", str(ir_path), "--out",
                                                 str(Path(d) / "t")]):
                with self.assertRaises(SystemExit) as cm:
                    with redirect_stdout(io.StringIO()):
                        runpy.run_path(str(ROOT / "tools" / "explorer.py"), run_name="__main__")
            self.assertEqual(cm.exception.code, 0)


if __name__ == "__main__":
    unittest.main()
