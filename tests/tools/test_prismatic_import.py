"""Tests for tools/prismatic_import.py: the Elixir literal parser and the import/check cycle."""

import io
import json
import os
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

import prismatic_import as imp  # noqa: E402

SOURCE = '''defmodule X do
  defp scenario_configs do
    %{
      drill: %{
        total_steps: 1,
        # a comment the parser skips
        briefing: "Part one " <>
          "and \\"two\\".",
        objectives: ["Stay calm"],
        rules: ["Trust matters"],
        steps: drill_steps()
      }
    }
  end

  def start_scenario(t) do
    %{trust_level: 50, other: :ok}
  end

  defp drill_steps do
    %{
      1 => %{
        prompt: "First?",
        options: [
          %{id: "a", text: "Calm", risk_level: :low,
            consequence: %{text: "Good.", type: :success, impact: :positive, score_impact: 10, trust_delta: 15}},
          %{id: "b", text: "Loud", risk_level: :high,
            consequence: %{text: "Bad.", type: :danger, impact: :negative, score_impact: -5, trust_delta: -20}}
        ]
      }
    }
  end
end
'''


def quiet(fn, *a):
    out, err = io.StringIO(), io.StringIO()
    with redirect_stdout(out), redirect_stderr(err):
        rc = fn(*a)
    return rc, out.getvalue(), err.getvalue()


class ParseTest(unittest.TestCase):
    def test_parse(self):
        data = imp.parse(SOURCE)
        self.assertEqual(data["start_trust"], 50)
        (sc,) = data["scenarios"]
        self.assertEqual(sc["id"], "drill")
        self.assertEqual(sc["briefing"], 'Part one and "two".')
        op = sc["steps"][0]["options"][1]
        self.assertEqual(op["risk"], "high")
        self.assertEqual(op["consequence"], {"text": "Bad.", "type": "danger",
                                             "impact": "negative", "trust_delta": -20})

    def test_errors(self):
        for bad, why in (
                (SOURCE.replace("total_steps: 1", "total_steps: 2"), "total_steps"),
                (SOURCE.replace("trust_level: 50", "trust: 50"), "starting trust"),
                (SOURCE.replace("drill_steps()", "other_steps()"), "unknown function"),
                (SOURCE.replace("defp scenario_configs", "defp configs"), "no defp"),
                (SOURCE.replace('prompt: "First?"', "prompt: @x"), "unexpected"),
                (SOURCE.replace("1 =>", "1 ,"), "expected arrow"),
                (SOURCE.replace("rules: [", "rules: {"), "unexpected open")):
            with self.assertRaisesRegex(imp.ParseError, why):
                imp.parse(bad)
        self.assertEqual(imp.tokens("  "), [])


class MainTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        base = Path(self.tmp.name)
        self.repo = base / "pp"
        src = self.repo / imp.PATH
        src.parent.mkdir(parents=True)
        src.write_text(SOURCE)
        git = ["git", "-C", str(self.repo)]
        subprocess.run(["git", "init", "-q", str(self.repo)], check=True)
        subprocess.run(git + ["add", "-A"], check=True)
        subprocess.run(git + ["-c", "user.email=t@t", "-c", "user.name=t", "commit", "-q",
                              "-m", "x"], check=True)
        commit = subprocess.run(git + ["rev-parse", "HEAD"], check=True, capture_output=True,
                                text=True).stdout.strip()
        (base / "knowledge").mkdir()
        self.out = base / "knowledge" / "scenarios.json"
        self.patches = [mock.patch.object(imp, "ROOT", base), mock.patch.object(imp, "OUT", self.out),
                        mock.patch.object(imp, "COMMIT", commit),
                        mock.patch.dict(os.environ, {"PRISMATIC_DIR": str(self.repo)})]
        for p in self.patches:
            p.start()

    def tearDown(self):
        for p in self.patches:
            p.stop()
        self.tmp.cleanup()

    def test_import_and_check(self):
        rc, _, err = quiet(imp.main, ["--check"])
        self.assertEqual(rc, 1)  # nothing imported yet
        self.assertIn("differs", err)
        rc, out, _ = quiet(imp.main, [])
        self.assertEqual((rc, "wrote" in out), (0, True))
        self.assertEqual(json.loads(self.out.read_text())["scenarios"][0]["id"], "drill")
        rc, out, _ = quiet(imp.main, ["--check"])
        self.assertEqual((rc, "current" in out), (0, True))
        self.out.write_text("{}")
        self.assertEqual(quiet(imp.main, ["--check"])[0], 1)

    def test_absent_source_and_entry_point(self):
        with mock.patch.dict(os.environ, {"PRISMATIC_DIR": str(Path(self.tmp.name) / "none")}):
            rc, out, _ = quiet(imp.main, ["--check"])
            self.assertEqual(rc, 0)
            self.assertIn("absent", out)
            with mock.patch.object(sys, "argv", ["prismatic_import.py", "--check"]), \
                    mock.patch.dict(os.environ, {"POCKET_ROOT": self.tmp.name}):
                with self.assertRaises(SystemExit) as cm:
                    quiet(runpy.run_path, str(ROOT / "tools" / "prismatic_import.py"), None,
                          "__main__")
            self.assertEqual(cm.exception.code, 0)


if __name__ == "__main__":
    unittest.main()
