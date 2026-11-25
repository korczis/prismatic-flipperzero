#!/usr/bin/env python3
"""Import the Prismatic Platform's crisis simulations into knowledge/scenarios.json.

  tools/prismatic_import.py           read the pinned commit, write knowledge/scenarios.json
  tools/prismatic_import.py --check   fail when the file differs from a fresh import; skipped
                                      (exit 0, said so) when the Prismatic Platform is absent

The source is an Elixir module whose scenario definitions are literal maps
(`PrismaticCrisis.ScenarioEngine`). It is read with `git show <commit>:<path>` and parsed as
data by a small literal parser: maps, lists, strings joined with `<>`, atoms, integers and
calls to zero-argument private functions that return a literal. Nothing is executed. The
Prismatic Platform is private, so the import is committed here and CI reads the JSON.
"""

import json
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(os.environ.get("POCKET_ROOT") or Path(__file__).resolve().parents[1])
OUT = ROOT / "knowledge" / "scenarios.json"
REPOSITORY = "https://gitlab.com/korczis/prismatic-platform"
COMMIT = "49adc18f7bac"
PATH = "apps/prismatic/lib/prismatic_crisis/scenario_engine.ex"

TOKEN = re.compile(r'''\s*(?:
    (?P<map>%\{) | (?P<open>[\[{]) | (?P<close>[\]}]) | (?P<comma>,) | (?P<arrow>=>) |
    (?P<concat><>) | (?P<key>[a-z_][a-zA-Z0-9_]*[?!]?):(?!:) |
    (?P<str>"(?:\\.|[^"\\])*") | (?P<atom>:[a-z_][a-zA-Z0-9_]*) | (?P<int>-?\d+) |
    (?P<call>[a-z_][a-zA-Z0-9_]*)\(\) | (?P<comment>\#[^\n]*)
)''', re.X)


class ParseError(Exception):
    pass


def tokens(text):
    at, out = 0, []
    while at < len(text):
        m = TOKEN.match(text, at)
        if not m or m.end() == at:
            if text[at:].strip() == "":
                break
            raise ParseError(f"unexpected {text[at:at + 30]!r}")
        at = m.end()
        kind = m.lastgroup
        if kind != "comment":
            out.append((kind, m.group(kind)))
    return out


class Parser:
    def __init__(self, toks, functions):
        self.t, self.i, self.functions = toks, 0, functions

    def peek(self):
        return self.t[self.i] if self.i < len(self.t) else ("end", "")

    def take(self, kind):
        k, v = self.peek()
        if k != kind:
            raise ParseError(f"expected {kind}, found {k} {v!r}")
        self.i += 1
        return v

    def value(self):
        k, v = self.peek()
        if k == "map":
            return self.mapping()
        if k == "open" and v == "[":
            return self.items()
        if k == "str":
            parts = [json.loads(self.take("str"))]
            while self.peek()[0] == "concat":
                self.take("concat")
                parts.append(json.loads(self.take("str")))
            return "".join(parts)
        if k == "atom":
            return self.take("atom")[1:]
        if k == "int":
            return int(self.take("int"))
        if k == "call":
            name = self.take("call")
            if name not in self.functions:
                raise ParseError(f"call to unknown function {name}()")
            return self.functions[name]()
        raise ParseError(f"unexpected {k} {v!r}")

    def mapping(self):
        self.take("map")
        out = {}
        while self.peek()[0] != "close":
            if self.peek()[0] == "key":
                key = self.take("key")
            else:
                key = self.value()
                self.take("arrow")
            out[key] = self.value()
            if self.peek()[0] == "comma":
                self.take("comma")
        self.take("close")
        return out

    def items(self):
        self.take("open")
        out = []
        while self.peek()[0] != "close":
            out.append(self.value())
            if self.peek()[0] == "comma":
                self.take("comma")
        self.take("close")
        return out


def literal_function(source, name):
    """The literal a zero-argument `defp name do <literal> end` returns, as text."""
    m = re.search(rf"\n  defp {name} do\n(.*?)\n  end\n", source, re.S)
    if not m:
        raise ParseError(f"no defp {name}")
    return m.group(1)


def parse(source):
    """Every scenario, in the source's order, plus the trust a run starts with."""
    functions = {}
    for name in re.findall(r"\n  defp ([a-z_]+_steps) do\n", source):
        functions[name] = (lambda n: lambda: Parser(
            tokens(literal_function(source, n)), functions).value())(name)
    configs = Parser(tokens(literal_function(source, "scenario_configs")), functions).value()
    start = re.search(r"trust_level: (\d+),", source)
    if not start:
        raise ParseError("no starting trust_level")
    scenarios = []
    for sid, c in configs.items():
        steps = [c["steps"][n] for n in sorted(c["steps"])]
        if len(steps) != c["total_steps"]:
            raise ParseError(f"{sid}: {len(steps)} steps, total_steps says {c['total_steps']}")
        scenarios.append({
            "id": sid, "briefing": c["briefing"], "objectives": c["objectives"],
            "rules": c["rules"],
            "steps": [{"prompt": s["prompt"], "options": [
                {"id": o["id"], "text": o["text"], "risk": o["risk_level"],
                 "consequence": {k: o["consequence"][k] for k in
                                 ("text", "type", "impact", "trust_delta")}}
                for o in s["options"]]} for s in steps]})
    return {"source": {"repository": REPOSITORY, "commit": COMMIT, "path": PATH},
            "start_trust": int(start.group(1)), "scenarios": scenarios}


def source_text(repo):
    return subprocess.run(["git", "-C", str(repo), "show", f"{COMMIT}:{PATH}"], check=True,
                          capture_output=True, text=True).stdout


def main(argv):
    repo = Path(os.environ.get("PRISMATIC_DIR") or Path.home() / "dev" / "prismatic-platform")
    if not (repo / ".git").exists():
        print(f"prismatic-import: {repo} is absent; {OUT.name} is used as committed")
        return 0
    text = json.dumps(parse(source_text(repo)), indent=1, ensure_ascii=False) + "\n"
    if "--check" in argv:
        if not OUT.exists() or OUT.read_text() != text:
            print(f"prismatic-import: {OUT.relative_to(ROOT)} differs from a fresh import; "
                  "run tools/prismatic_import.py", file=sys.stderr)
            return 1
        print(f"prismatic-import: {OUT.relative_to(ROOT)} is current")
        return 0
    OUT.write_text(text)
    print(f"prismatic-import: wrote {OUT.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
