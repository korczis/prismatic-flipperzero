#!/usr/bin/env python3
"""Render the GitHub wiki from the repository's own Markdown, so the wiki never says anything
the repository does not.

  tools/wiki.py <wiki-checkout>      write the pages into a clone of <repo>.wiki.git

Each page in PAGES is a repository file with its relative links rewritten: links to another
wiki page become wiki links, images and every other file point at the file on main. A
_Sidebar.md lists the pages and a footer on each page names its source file.
"""

import os
import re
import sys
from pathlib import Path

ROOT = Path(os.environ.get("POCKET_ROOT") or Path(__file__).resolve().parent.parent)
REPO = "https://github.com/korczis/prismatic-flipperzero"
PAGES = [
    ("Home", "README.md"),
    ("Case-Study", "docs/case-study.md"),
    ("Architecture", "docs/architecture.md"),
    ("Development", "docs/development.md"),
    ("Interface", "docs/ui.md"),
]
LINK = re.compile(r'(\]\(|src=")(?!https?:|#|mailto:)([^)"\s]+)')
REGION = re.compile(r"^<!-- docs:(begin|end) [\w-]+ -->\n", re.M)


def rewrite(text, source):
    by_file = {path: name for name, path in PAGES}
    base = Path(source).parent

    def fix(m):
        target, _, anchor = m.group(2).partition("#")
        path = (base / target).as_posix()
        parts = []
        for part in path.split("/"):
            if part == "..":
                parts.pop()
            elif part != ".":
                parts.append(part)
        path = "/".join(parts).rstrip("/")
        suffix = f"#{anchor}" if anchor else ""
        if path in by_file:
            return f"{m.group(1)}{by_file[path]}{suffix}"
        kind = "raw" if m.group(1) == 'src="' or path.endswith((".png", ".svg")) else "blob"
        return f"{m.group(1)}{REPO}/{kind}/main/{path}{suffix}"

    return LINK.sub(fix, REGION.sub("", text))


def render(out):
    out = Path(out)
    written = []
    for name, source in PAGES:
        body = rewrite((ROOT / source).read_text(), source)
        footer = f"\n\n---\n_Generated from [`{source}`]({REPO}/blob/main/{source}) by " \
                 f"`tools/wiki.py`; edit the source, not this page._\n"
        (out / f"{name}.md").write_text(body.rstrip("\n") + footer)
        written.append(name)
    sidebar = "\n".join(f"- [{n.replace('-', ' ')}]({n})" for n, _ in PAGES)
    (out / "_Sidebar.md").write_text(
        f"**Prismatic Pocket**\n\n{sidebar}\n- [Explorer](https://korczis.github.io/"
        f"prismatic-flipperzero/)\n- [Releases]({REPO}/releases)\n")
    return written


def main(argv):
    if len(argv) != 1:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    print("wrote " + ", ".join(render(argv[0])))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
