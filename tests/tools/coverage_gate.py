#!/usr/bin/env python3
"""Line-coverage gate for tools/*.py, with no third-party dependency (sys.monitoring, 3.12+).

Runs the unittest suite in tests/tools with LINE events recorded for every file under tools/,
derives each file's executable lines from its compiled code objects, and fails listing every
executable line no test ran, by file and line. Exemptions live in coverage-exemptions.json,
each with a written reason, and are printed on every run.

Usage: python3 tests/tools/coverage_gate.py
"""

import json
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "tools"
EXEMPTIONS = json.loads((Path(__file__).with_name("coverage-exemptions.json")).read_text())


def executable_lines(path):
    lines, stack = set(), [compile(path.read_text(), str(path), "exec")]
    while stack:
        code = stack.pop()
        lines.update(ln for _s, _e, ln in code.co_lines() if ln)
        stack.extend(c for c in code.co_consts if hasattr(c, "co_lines"))
    return lines


def main():
    mon = sys.monitoring
    tool = mon.COVERAGE_ID
    mon.use_tool_id(tool, "prismatic-coverage")
    hits = {}
    prefix = str(TOOLS) + "/"

    def on_line(code, line):
        if code.co_filename.startswith(prefix):
            hits.setdefault(code.co_filename, set()).add(line)
            return None
        return mon.DISABLE

    mon.register_callback(tool, mon.events.LINE, on_line)
    mon.set_events(tool, mon.events.LINE)
    try:
        suite = unittest.defaultTestLoader.discover(str(Path(__file__).parent))
        result = unittest.TextTestRunner(verbosity=0).run(suite)
    finally:
        mon.set_events(tool, 0)
        mon.free_tool_id(tool)
    if not result.wasSuccessful():
        print("coverage: tests failed", file=sys.stderr)
        return 1

    missing = []
    for path in sorted(TOOLS.glob("*.py")):
        rel = str(path.relative_to(ROOT))
        exempt = {int(k): v for k, v in EXEMPTIONS.get(rel, {}).items()}
        for line, reason in sorted(exempt.items()):
            print(f"EXEMPT {rel}:{line} — {reason}")
        lost = sorted(executable_lines(path) - hits.get(str(path), set()) - set(exempt))
        total = len(executable_lines(path))
        print(f"{rel}: {total - len(lost)}/{total} lines")
        missing += [f"{rel}:{ln}" for ln in lost]
    if missing:
        print("coverage: lines no test ran:\n  " + "\n  ".join(missing), file=sys.stderr)
        return 1
    print("coverage: every executable line in tools/*.py ran")
    return 0


if __name__ == "__main__":
    sys.exit(main())
