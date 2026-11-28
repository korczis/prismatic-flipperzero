"""Tests for tools/release_record.py. Run: python3 -m unittest discover -s tests/tools"""

import io
import json
import runpy
import sys
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

import release_record as rr  # noqa: E402

FAP, PACK = "a" * 64, "b" * 64
URL = "https://github.com/o/r/releases"


def asset(name, digest, size=10):
    return {"name": name, "digest": f"sha256:{digest}", "size": size,
            "url": f"{URL}/download/v1.2.3/{name}"}


class FakeGh:
    """Answers the three questions the tool asks: the release, the tag's commit, SHA256SUMS."""

    def __init__(self, assets=None, sums=None, prerelease=True, draft=False,
                 tags="v1.2.3\nv1.2\nlatest\n"):
        self.assets = assets if assets is not None else [
            asset("prismatic-1.2.3-official-api88.2.fap", FAP),
            asset("kompendium-1.2.3.pkp", PACK, 2048),
            asset("SHA256SUMS", "c" * 64),
        ]
        self.sums = sums if sums is not None else (
            f"{FAP}  prismatic-1.2.3-official-api88.2.fap\n{PACK} *kompendium-1.2.3.pkp\n\n")
        self.prerelease, self.draft, self.tags = prerelease, draft, tags

    def __call__(self, cmd):
        if cmd[:3] == ["gh", "release", "view"]:
            return json.dumps({"tagName": cmd[3], "publishedAt": "2026-11-28T05:35:07Z",
                               "url": f"{URL}/tag/{cmd[3]}", "isPrerelease": self.prerelease,
                               "isDraft": self.draft,
                               "assets": self.assets})
        if cmd[:2] == ["git", "rev-list"]:
            return "f" * 40 + "\n"
        if cmd[:3] == ["gh", "release", "download"]:
            return self.sums
        if cmd[:3] == ["git", "tag", "-l"]:
            return self.tags
        raise AssertionError(cmd)


def quiet(fn, *a):
    out, err = io.StringIO(), io.StringIO()
    with redirect_stdout(out), redirect_stderr(err):
        rc = fn(*a)
    return rc, out.getvalue(), err.getvalue()


class RecordTest(unittest.TestCase):
    def test_record_is_read_back_from_the_release(self):
        text = rr.record("v1.2.3", FakeGh())
        self.assertIn('version: "1.2.3"\ntag: v1.2.3\nchannel: prerelease\n', text)
        self.assertIn("commit: " + "f" * 40, text)
        self.assertIn(f"notes_url: {URL}/tag/v1.2.3", text)
        # sorted by name, the version taken out of each target id
        self.assertLess(text.index("target: SHA256SUMS"), text.index("target: kompendium.pkp"))
        self.assertIn("  - target: kompendium.pkp\n    name: kompendium-1.2.3.pkp\n", text)
        self.assertIn(f"    sha256: {PACK}\n    size: 2048\n", text)
        self.assertIn("target: prismatic-official-api88.2.fap", text)
        self.assertIn("channel: stable", rr.record("v1.2.3", FakeGh(prerelease=False)))
        # a version with a pre-release part is never stable, whatever GitHub says
        pre = FakeGh(prerelease=False, assets=[], sums="")
        self.assertIn('version: "1.2.3-rc.1"\ntag: v1.2.3-rc.1\nchannel: prerelease\n',
                      rr.record("v1.2.3-rc.1", pre))

    def test_refusals(self):
        cases = [
            ("v1.2", FakeGh(), "not a vMAJOR"),
            ("v1.2.3", FakeGh(assets=[{"name": "x", "size": 1, "url": URL}]), "no SHA-256"),
            ("v1.2.3", FakeGh(assets=[{"name": "x", "size": 1, "url": URL, "digest": None}]),
             "no SHA-256"),
            ("v1.2.3", FakeGh(assets=[asset("SHA256SUMS", "c" * 64, size=0)]), "is empty"),
            ("v1.2.3", FakeGh(draft=True), "a draft"),
            ("v1.2.3", FakeGh(sums=f"{FAP}\n"), "not a '<sha256>  <name>' line"),
            ("v1.2.3", FakeGh(sums="SHA256 (a.fap) = x\n"), "not a '<sha256>  <name>' line"),
            ("v1.2.3", FakeGh(sums=f"{PACK}  prismatic-1.2.3-official-api88.2.fap\n"),
             "does not match"),
            ("v1.2.3", FakeGh(sums=FakeGh().sums + f"{FAP}  gone.fap\n"), "lacks: gone.fap"),
        ]
        for tag, gh, message in cases:
            with self.subTest(message=message):
                with self.assertRaisesRegex(rr.RecordError, message):
                    rr.record(tag, gh)

    def test_main_writes_every_tag_and_reports_a_refusal(self):
        with tempfile.TemporaryDirectory() as d:
            rc, out, _ = quiet(rr.main, ["--all", "--out-dir", d], FakeGh())
            self.assertEqual(rc, 0)
            self.assertEqual(sorted(p.name for p in Path(d).iterdir()), ["v1.2.3.yaml"])
            self.assertIn("v1.2.3.yaml", out)
            rc, _, err = quiet(rr.main, ["v1.2.3", "--out-dir", d], FakeGh(sums=""))
            self.assertEqual(rc, 1)
            self.assertIn("release-record: v1.2.3", err)
        def no_git(cmd):
            raise rr.RecordError("git tag -l: not a git repository")

        rc, _, err = quiet(rr.main, ["--all"], no_git)
        self.assertEqual(rc, 1)
        self.assertIn("not a git repository", err)
        rc, _, err = quiet(rr.main, [], FakeGh())
        self.assertEqual(rc, 2)
        self.assertIn("Usage", err)

    def test_run_and_entry_point(self):
        self.assertEqual(rr.run([sys.executable, "-c", "print(40)"]), "40\n")
        with self.assertRaisesRegex(rr.RecordError, "release not found"):
            rr.run([sys.executable, "-c", "import sys; sys.exit('release not found')"])
        with self.assertRaisesRegex(rr.RecordError, "exit 3"):
            rr.run([sys.executable, "-c", "raise SystemExit(3)"])
        with mock.patch.object(sys, "argv", ["release_record.py"]):
            with self.assertRaises(SystemExit) as cm:
                quiet(runpy.run_path, str(ROOT / "tools" / "release_record.py"), None,
                      "__main__")
        self.assertEqual(cm.exception.code, 2)


if __name__ == "__main__":
    unittest.main()
