#!/usr/bin/env python3
"""Write the Majordomus release record of a published release (.ai/repo/releases/<tag>.yaml).

The record is evidence of what was published, read back from GitHub rather than typed: the
tag's commit from git, and for every asset its name, URL, size and SHA-256. Each digest
GitHub reports is checked against the release's own SHA256SUMS, so two independent readings
of every file have to agree before a record is written.

Usage: tools/release_record.py <tag>... | --all   [--out-dir DIR]
"""

import json
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / ".ai" / "repo" / "releases"
TAG = re.compile(r"^v(\d+\.\d+\.\d+(?:-[0-9A-Za-z.-]+)?)$")  # release/v1's tag pattern
SUMS = "SHA256SUMS"


class RecordError(Exception):
    pass


def run(cmd):
    """A command's output; a failure is a refusal carrying what the command said."""
    done = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    if done.returncode:
        said = (done.stderr or done.stdout).strip() or f"exit {done.returncode}"
        raise RecordError(f"{' '.join(cmd[:3])}: {said}")
    return done.stdout


def target(name, version):
    """The asset's id across releases: its name with the version taken out."""
    return name.replace(f"-{version}", "")


def sums(text):
    out = {}
    for line in text.splitlines():
        if line.strip():
            parts = line.split(maxsplit=1)
            if len(parts) != 2 or not re.fullmatch(r"[0-9a-f]{64}", parts[0]):
                raise RecordError(f"{SUMS}: not a '<sha256>  <name>' line: {line.strip()!r}")
            out[parts[1].strip().lstrip("*")] = parts[0]
    return out


def record(tag, sh=run):
    m = TAG.match(tag)
    if not m:
        raise RecordError(f"{tag}: not a vMAJOR.MINOR.PATCH[-PRE] tag")
    version = m.group(1)
    rel = json.loads(sh(["gh", "release", "view", tag, "--json",
                         "tagName,publishedAt,url,isPrerelease,isDraft,assets"]))
    if rel["isDraft"]:
        raise RecordError(f"{tag}: the release is a draft, not published")
    commit = sh(["git", "rev-list", "-n", "1", tag]).strip()
    listed = sums(sh(["gh", "release", "download", tag, "-p", SUMS, "-O", "-"]))
    lines = [
        "# Written by tools/release_record.py from the release GitHub publishes.",
        f"# Evidence, not a plan: every digest below was read by GitHub and matches {SUMS}.",
        "schema: release/v1",
        f'version: "{version}"',
        f"tag: {tag}",
        f"channel: {'prerelease' if rel['isPrerelease'] or '-' in version else 'stable'}",
        f"commit: {commit}",
        f'published_at: "{rel["publishedAt"]}"',
        f"notes_url: {rel['url']}",
        "artifacts:",
    ]
    names = set()
    for a in sorted(rel["assets"], key=lambda a: a["name"]):
        name, digest = a["name"], (a.get("digest") or "").removeprefix("sha256:")
        if not re.fullmatch(r"[0-9a-f]{64}", digest):
            raise RecordError(f"{tag}: GitHub reports no SHA-256 for {name}")
        if a["size"] < 1:
            raise RecordError(f"{tag}: {name} is empty")
        if name != SUMS and listed.get(name) != digest:
            raise RecordError(f"{tag}: {name} does not match {SUMS}")
        names.add(name)
        lines += [f"  - target: {target(name, version)}", f"    name: {name}",
                  f"    url: {a['url']}", f"    sha256: {digest}", f"    size: {a['size']}"]
    missing = sorted(set(listed) - names)
    if missing:
        raise RecordError(f"{tag}: {SUMS} lists files the release lacks: {', '.join(missing)}")
    return "\n".join(lines) + "\n"


def main(argv, sh=run):
    args, out = list(argv), OUT
    if "--out-dir" in args:
        i = args.index("--out-dir")
        out = Path(args[i + 1])
        del args[i:i + 2]
    try:
        tags = [t for t in sh(["git", "tag", "-l", "v*"]).split() if TAG.match(t)] \
            if args == ["--all"] else args
        if not tags:
            print(__doc__.strip().splitlines()[-1], file=sys.stderr)
            return 2
        out.mkdir(parents=True, exist_ok=True)
        for tag in tags:
            text = record(tag, sh)
            (out / f"{tag}.yaml").write_text(text)
            print(f"release-record: {out / f'{tag}.yaml'}")
    except RecordError as e:
        print(f"release-record: {e}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
