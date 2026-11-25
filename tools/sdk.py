#!/usr/bin/env python3
"""Install, check or describe a pinned SDK from tools/sdks.json.

  tools/sdk.py install <id>   download the SDK zip, verify its SHA-256, `ufbt update --local`
  tools/sdk.py check <id>     exit 0 when that SDK is the one installed in its UFBT_HOME
  tools/sdk.py env <id>       print `UFBT_HOME=...` for the entry (for `env $(...) ufbt`)
  tools/sdk.py list           print the matrix as tab-separated id, version, status

Each entry has its own UFBT_HOME so building for Momentum never replaces the official SDK.
A non-default home shares the toolchain of ~/.ufbt through a symlink when one exists.
"""

import hashlib
import json
import os
import subprocess
import sys
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MATRIX = json.loads((ROOT / "tools" / "sdks.json").read_text())["sdks"]


def entry(sdk_id):
    for e in MATRIX:
        if e["id"] == sdk_id:
            return e
    sys.exit(f"unknown SDK id {sdk_id!r}; known: {', '.join(e['id'] for e in MATRIX)}")


def home(e):
    return Path(os.path.expanduser(e["ufbt_home"]))


def installed_zip(e):
    return home(e) / "download" / Path(e["url"]).name


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def check(e):
    state = home(e) / "current" / "ufbt_state.json"
    marker = home(e) / "current" / ".prismatic-sdk"
    if not state.exists() or not marker.exists():
        return False
    return marker.read_text().strip() == e["sha256"]


def install(e):
    h = home(e)
    (h / "download").mkdir(parents=True, exist_ok=True)
    shared = Path(os.path.expanduser("~/.ufbt/toolchain"))
    if h != Path(os.path.expanduser("~/.ufbt")) and shared.is_dir():
        link = h / "toolchain"
        if not link.exists():
            link.symlink_to(shared)
    z = installed_zip(e)
    if not z.exists() or sha256(z) != e["sha256"]:
        print(f"downloading {e['url']}", file=sys.stderr)
        tmp = z.with_suffix(".part")
        req = urllib.request.Request(e["url"], headers={"User-Agent": "prismatic-flipperzero-sdk"})
        with urllib.request.urlopen(req, timeout=120) as r, open(tmp, "wb") as f:
            while block := r.read(1 << 20):
                f.write(block)
        tmp.rename(z)
    got = sha256(z)
    if got != e["sha256"]:
        z.unlink()
        sys.exit(f"SHA-256 mismatch for {z.name}: expected {e['sha256']}, got {got}")
    env = dict(os.environ, UFBT_HOME=str(h))
    subprocess.run(["ufbt", "update", "--hw-target", "f7", "--local", str(z)], env=env, check=True)
    (h / "current" / ".prismatic-sdk").write_text(e["sha256"] + "\n")
    print(f"installed {e['label']} into {h}")


def main(argv):
    if not argv or argv[0] not in ("install", "check", "env", "list"):
        print(__doc__, file=sys.stderr)
        return 2
    if argv[0] == "list":
        for e in MATRIX:
            print(f"{e['id']}\t{e['version']}\t{e['status']}")
        return 0
    e = entry(argv[1] if len(argv) > 1 else "official")
    if argv[0] == "env":
        print(f"UFBT_HOME={home(e)}")
        return 0
    if argv[0] == "check":
        ok = check(e)
        print(f"{e['id']}: {'installed' if ok else 'not installed'} ({e['version']})")
        return 0 if ok else 1
    install(e)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
