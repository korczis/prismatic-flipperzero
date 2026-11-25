"""Tests for tools/pocket.py against a small git repository shaped like the Kompendium."""

import io
import json
import runpy
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

import pocket  # noqa: E402

TECHNIQUE = """+++
title = "EA-1.2.1 Falešné dilema"
description = "Dvě možnosti místo spojité škály."
[taxonomies]
tags = ["kompendium", "techniques"]
[extra]
ea_code = "EA-1.2.1"
keywords = ["False dilemma", "EA-1.2.1 Falešné dilema"]
related_terms = ["ea-1-2-2-falesna-ekvivalence", "missing-slug"]
+++
Úvodní odstavec, který je dost dlouhý na to, aby se stal kapitolou bez nadpisu, protože má
víc než sto šedesát znaků a nese obsah, který kniha píše před první sekcí stránky celé.

## Definice

Složitý problém je předložen jako volba mezi dvěma extrémy.

Druhý odstavec definice prodlužuje sekci natolik, že se zachová i jako samostatná kapitola
čtečky, protože je o víc než čtyřicet znaků delší než první odstavec.

## Detekční markery

| Marker | Indikuje |
|--------|----------|
| Problém **binárně** | Strukturální dilema |
| Žádné alternativy | Vyloučení |

### Podsekce

1. Číslovaný marker

## Diferenciální detekce

Skutečné dilema existuje. Viz [ekvivalence](@/kompendium/techniques/ea-1-2-2-falesna-ekvivalence.md).

## Mechanismus

Text kapitoly s *kurzívou*, `kódem` a [odkazem](@/glossary/heuristika.md#kotva).

## Literatura

- Zdroj, který se přeskočí.
"""

QUICK = """+++
title = "Rychlá karta EA-1.2.1: Falešné dilema"
[extra]
ea_code = "EA-1.2.1"
related_terms = ["qr-ea-1-2-2"]
+++
## Co to je

Složitý problém se předkládá jako volba mezi dvěma extrémy.

## Jak ji poznat

- Problém je podán binárně.
- Alternativy se nevyjmenují.

## První bezpečná reakce

**Pro zasaženého:** pojmenujte rámec.

**Pro odborníka:** sledujte posun.

## Kdy a jak eskalovat

Když se dilema opakuje.

## S čím ji nezaměnit

- **Falešná ekvivalence:** viz [kartu](@/kompendium/quick-reference/qr-ea-1-2-2.md).
"""

ORPHAN_CARD = """+++
title = "Rychlá karta EA-9.9.9: Sirotek"
[extra]
ea_code = "EA-9.9.9"
+++
## Co to je

Karta bez techniky zůstane samostatným objektem s vlastní definicí.
"""

EQUIVALENCE = """+++
title = "EA-1.2.2 Falešná ekvivalence"
[extra]
ea_code = "EA-1.2.2"
+++
Krátký úvod.

## Definice

Dvě pozice se vydávají za stejně silné, i když nejsou.
"""

GLOSSARY = """+++
title = "Heuristika"
[extra]
related_terms = ["ea-1-2-1-falesne-dilema"]
+++
## Definice

Úsporná strategie úsudku.
"""

FALLACY = """+++
title = "Útok na osobu"
description = "Napadá se člověk místo argumentu."
[extra]
catalog_id = "AF-01.01"
+++
## Obrana a odpověď

- Vrátit se k argumentu.
"""

CATALOG = '''
[[pattern]]
id = "AF-01.01"
slug = "utok-na-osobu"
cs = "Útok na osobu"
en = "Ad hominem"
aliases = ["argumentum ad hominem"]
related = ["sirotci-klam"]
review = "agent"

[[pattern.trainer]]
utterance = "Ty o tom nemůžeš mluvit, jsi mladý."
context = "Porada."
benign = false
marker = "Útok na osobu."
response = "Vrátit se k věci."

[[pattern]]
id = "AF-01.02"
slug = "ekvivalence"
cs = "Ekvivalence"
canonical = "kompendium/techniques/ea-1-2-2-falesna-ekvivalence.md"

[[pattern]]
id = "AF-01.03"
slug = "sirotci-klam"
cs = "Sirotčí klam"
en = "Orphan fallacy"
kompendium = ["kompendium/techniques/ea-1-2-1-falesne-dilema.md"]

[[pattern.trainer]]
utterance = "Spínač má dvě polohy."
benign = true

[[pattern]]
id = "AF-01.04"
slug = "sirotci-klam"
cs = "Sirotčí klam znovu"
'''


def spec(commit, exclude=("_index.md",)):
    return {
        "pack": {"id": "test", "title": "Test", "version": "1", "generator": "test",
                 "licence": "test"},
        "source": {"repository": "https://example.invalid/kompendium", "commit": commit},
        "pages": {"roots": ["content/kompendium", "content/glossary"], "exclude": list(exclude)},
        "title_strip": ["^Rychlá karta [^:]+:\\s*"],
        "drop_tags": ["kompendium"],
        "kinds": {"techniques": "technique", "quick-reference": "technique",
                  "argumentacni-fauly": "fallacy", "glossary": "concept", "default": "article"},
        "overlay": {"from_prefix": "kompendium/quick-reference/"},
        "roles": {
            "skip": ["literatura"], "definition": ["co to je", "definice"],
            "indicators": ["jak ji poznat", "detekční markery"],
            "actions": ["první bezpečná reakce", "obrana"],
            "escalation": ["kdy a jak eskalovat"],
            "alternatives": ["s čím ji nezaměnit", "diferenciální"],
            "validation": ["validační test"]},
        "catalogs": [{"path": "data/fauly", "include": "^[a-z].*\\.toml$", "kind": "fallacy",
                      "prefix": "fauly", "category": "Fauly (katalog)"}],
    }


def make_source(d, broken=True):
    root = Path(d)
    files = {
        "content/kompendium/_index.md": '+++\ntitle = "Kompendium"\n+++\n',
        "content/kompendium/techniques/_index.md": '+++\ntitle = "Techniques"\n+++\n',
        "content/kompendium/techniques/ea-1-2-1-falesne-dilema.md": TECHNIQUE,
        "content/kompendium/techniques/ea-1-2-2-falesna-ekvivalence.md": EQUIVALENCE,
        "content/kompendium/quick-reference/qr-ea-1-2-1.md": QUICK,
        "content/kompendium/quick-reference/qr-ea-9-9-9.md": ORPHAN_CARD,
        "content/kompendium/quick-reference/notes.txt": "not markdown",
        "content/kompendium/argumentacni-fauly/relevance/ad-hominem.md": FALLACY,
        "content/glossary/heuristika.md": GLOSSARY,
        "data/fauly/relevance.toml": CATALOG,
        "data/fauly/_coverage.toml": "x = 1\n",
    }
    if broken:
        files["content/glossary/broken.md"] = "no front matter here\n"
    for rel, text in files.items():
        p = root / rel
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(text)
    run = lambda *a: subprocess.run(["git", "-C", d, *a], check=True, capture_output=True)  # noqa
    run("init", "-q")
    run("-c", "user.email=t@t", "-c", "user.name=t", "add", "-A")
    run("-c", "user.email=t@t", "-c", "user.name=t", "commit", "-qm", "fixture")
    return subprocess.run(["git", "-C", d, "rev-parse", "HEAD"], check=True,
                          capture_output=True, text=True).stdout.strip()


def quiet(fn, *a):
    out, err = io.StringIO(), io.StringIO()
    with redirect_stdout(out), redirect_stderr(err):
        rc = fn(*a)
    return rc, out.getvalue(), err.getvalue()


class Fixture(unittest.TestCase):
    broken = True

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.src = Path(self.tmp.name) / "src"
        self.src.mkdir()
        commit = make_source(str(self.src), broken=self.broken)
        self.spec_path = Path(self.tmp.name) / "sources.json"
        self.spec_path.write_text(json.dumps(spec(commit)))
        self.patch = mock.patch.object(pocket, "SOURCES", self.spec_path)
        self.patch.start()
        self.root_patch = mock.patch.object(pocket, "ROOT", Path(self.tmp.name))
        self.root_patch.start()

    def tearDown(self):
        self.patch.stop()
        self.root_patch.stop()
        self.tmp.cleanup()


class ExtractTest(Fixture):
    def test_extract_merges_overlays_catalogue_and_resolves(self):
        ir, report = pocket.extract(self.src)
        by = {o["id"]: o for o in ir["objects"]}
        self.assertEqual(report["pages"], 7)  # broken.md counts, notes.txt and _index do not
        self.assertEqual(len(report["rejected"]), 1)
        self.assertEqual(report["overlays_merged"], 1)
        self.assertEqual(report["catalog_entries"], 4)
        self.assertEqual(report["catalog_merged"], 2)
        self.assertEqual(report["catalog_standalone"], 1)
        self.assertEqual(report["trainer_items"], 2)
        tech = by["kompendium/techniques/ea-1-2-1-falesne-dilema"]
        self.assertEqual(tech["indicators"], ["Problém je podán binárně.",
                                              "Alternativy se nevyjmenují."])
        self.assertEqual(len(tech["actions"]), 2)
        self.assertEqual(tech["escalation"], ["Když se dilema opakuje."])
        self.assertTrue(tech["definition"].startswith("Složitý problém se"))
        headings = [c["heading"] for c in tech["chapters"]]
        self.assertIn("Mechanismus", headings)
        self.assertIn("Definice", headings)  # the long definition survives as a chapter
        self.assertNotIn("Literatura", headings)
        self.assertEqual(tech["chapters"][0]["heading"], tech["title"])  # untitled preamble
        rels = {(r["type"], r["target"]) for r in tech["relations"]}
        self.assertIn(("confused_with", "kompendium/techniques/ea-1-2-2-falesna-ekvivalence"),
                      rels)
        self.assertIn(("related_to", "glossary/heuristika"), rels)
        self.assertNotIn("kompendium/quick-reference/qr-ea-1-2-1", by)
        self.assertIn("kompendium/quick-reference/qr-ea-9-9-9", by)
        fallacy = by["kompendium/argumentacni-fauly/relevance/ad-hominem"]
        self.assertEqual(fallacy["code"], "AF-01.01")
        self.assertIn("Ad hominem", fallacy["aliases"])
        self.assertEqual(fallacy["examples"][0]["benign"], False)
        self.assertEqual(fallacy["definition"], "Napadá se člověk místo argumentu.")
        standalone = by["fauly/sirotci-klam"]
        self.assertEqual(standalone["kind"], "fallacy")
        self.assertEqual(standalone["examples"][0]["context"], "")
        self.assertIn("ad-hominem", json.dumps(fallacy["relations"]) + "ad-hominem")
        equiv = by["kompendium/techniques/ea-1-2-2-falesna-ekvivalence"]
        self.assertEqual(equiv["code"], "EA-1.2.2")
        self.assertEqual(by["glossary/heuristika"]["kind"], "concept")
        for o in ir["objects"]:
            self.assertNotIn("links", o)
        pocket.validate(ir)

    def test_extract_adds_the_applications(self):
        apps = Path(self.tmp.name) / pocket.APPLICATIONS
        apps.parent.mkdir(parents=True)
        apps.write_text(
            '[[application]]\nid = "applications/t"\ntitle = "T"\ndefinition = "D"\n'
            'provenance = { repository = "r", commit = "c", path = "p" }\n'
            '[[application.axis]]\nname = "S"\nlevels = [["A", "a", ""], ["B", "b", ""]]\n')
        ir, report = pocket.extract(self.src)
        self.assertEqual(report["applications"], 1)
        self.assertIn("applications/t", {o["id"] for o in ir["objects"]})

    def test_markdown_helpers(self):
        self.assertEqual(pocket.plain("**a** [b](x) `c` *d*"), "a b c d")
        self.assertEqual(pocket.items_of("Jen odstavec.\n\nDruhý."), ["Jen odstavec.", "Druhý."])
        long = "Slovo " * 400
        pieces = pocket.chunks(long.strip(), 100)
        self.assertTrue(all(len(p) <= 100 for p in pieces))
        self.assertEqual(pocket.chunks("x" * 250 + ". Konec.", 100)[0], "x" * 100)
        self.assertEqual(pocket.chunks("Krátké. " + "y" * 120, 100)[0], "Krátké.")
        with self.assertRaises(pocket.PocketError):
            pocket.split_frontmatter("bez hlavičky")
        with self.assertRaises(pocket.PocketError):
            pocket.collection_of("elsewhere/x.md", ["content/kompendium"])
        self.assertEqual(pocket.collection_of("content/glossary/x.md", ["content/glossary"]),
                         "glossary")
        self.assertEqual(pocket.sections("intro\n## A\ntext"), [("", "intro"), ("A", "text")])
        self.assertEqual(pocket.fold("Žluťoučký „kůň“ – ∑"), 'Zlutoucky "kun" - ?')
        self.assertEqual(pocket.clip("abcdef", 5), "ab...")
        self.assertEqual(pocket.mask("Falešné dilema je dilema falešné.", "Falešné dilema"),
                         "___ je ___ ___.")
        self.assertEqual(pocket.summary("OONU?A")["observed"], 2)
        with mock.patch.dict(pocket.os.environ, {}, clear=True):
            self.assertTrue(str(pocket.default_source()).endswith("kompendium-epistemicke-anihilace"))
        with mock.patch.dict(pocket.os.environ, {"KOMPENDIUM_DIR": "/src/k"}):
            self.assertEqual(str(pocket.default_source()), "/src/k")
        self.assertEqual(pocket.chunks("Aaaa aaaa. Bbbb bbbb. Cccc.", 12),
                         ["Aaaa aaaa.", "Bbbb bbbb.", "Cccc."])

    def test_definition_falls_back_to_chapter_then_title(self):
        sp = json.loads(self.spec_path.read_text())
        prov = {"repository": "r", "commit": "c"}
        page = "+++\ntitle = \"T\"\n+++\n## Kapitola\n\nText kapitoly.\n"
        o = pocket.extract_page(page, "content/glossary/t.md", sp, {}, prov)
        self.assertEqual(o["definition"], "Text kapitoly.")
        o = pocket.extract_page("+++\ntitle = \"Jen titul\"\n+++\n", "content/glossary/u.md",
                                sp, {}, prov)
        self.assertEqual(o["definition"], "Jen titul")


def tiny_ir(n=5):
    objs = []
    for i in range(n):
        objs.append({
            "id": f"x/{i}", "kind": "technique", "title": f"Titul {i}", "code": f"C{i}",
            "category": "A" if i < 3 else "B", "definition": f"Definice číslo {i} " * 6,
            "indicators": ["ind"] if i % 2 == 0 else [], "actions": ["act"] if i == 1 else [],
            "escalation": ["esc"] if i == 3 else [], "alternatives": ["alt"] if i == 2 else [],
            "validation": ["val"] if i == 0 else [], "chapters": [{"heading": "H", "text": "T"}],
            "examples": [{"utterance": "U", "context": "C", "benign": i == 0, "marker": "M",
                          "response": "R"}] if i < 2 else [],
            "relations": [{"type": "confused_with", "target": f"x/{(i + 1) % n}"}],
            "provenance": {"repository": "r", "commit": "c", "path": "p"}})
    return {"schema": pocket.SCHEMA, "pack": {"id": "t", "title": "T", "version": "1"},
            "objects": objs}


def reseal(d):
    n = struct.unpack_from("<H", d, 16)[0]
    so, sl = len(d), 0
    for k in range(min(n, 32)):
        t, _e, off, length, _c = struct.unpack_from("<HHIII", d, 32 + 16 * k)
        if t == 2:
            so, sl = off, length
    d[12:16] = struct.pack("<I", zlib.crc32(bytes(d[so + sl:]), zlib.crc32(bytes(d[32:so]))))


def section(d, kind):
    n = struct.unpack_from("<H", d, 16)[0]
    for k in range(n):
        at = 32 + 16 * k
        if struct.unpack_from("<H", d, at)[0] == kind:
            return at
    raise AssertionError(kind)


class PackTest(unittest.TestCase):
    def setUp(self):
        self.data = pocket.encode(tiny_ir())

    def test_round_trip_and_determinism(self):
        self.assertEqual(self.data, pocket.encode(tiny_ir()))
        dump = pocket.decode(self.data, verify_strings=True)
        self.assertEqual(len(dump["objects"]), 5)
        self.assertEqual({q["kind"] for q in dump["quiz"]}, {1, 2})
        caps = {c for o in dump["objects"] for c in o["caps"]}
        self.assertEqual(caps, {"browse", "detect", "apply", "quiz", "compare"})
        self.assertEqual(dump["meta"]["pack_id"], "t")

    def test_no_quiz_without_three_distractors(self):
        ir = tiny_ir(2)
        for o in ir["objects"]:
            o["examples"] = []
        dump = pocket.decode(pocket.encode(ir))
        self.assertEqual(dump["quiz"], [])

    def bad(self, mutate, msg, verify=False):
        d = bytearray(self.data)
        mutate(d)
        with self.assertRaisesRegex(pocket.PocketError, msg):
            pocket.decode(bytes(d), verify_strings=verify)

    def test_rejections(self):
        with self.assertRaisesRegex(pocket.PocketError, "truncated"):
            pocket.decode(b"PKP1")
        self.bad(lambda d: d.__setitem__(0, ord("X")), "magic")
        self.bad(lambda d: struct.pack_into("<H", d, 4, 2), "version")
        self.bad(lambda d: struct.pack_into("<H", d, 6, 1), "features")
        self.bad(lambda d: struct.pack_into("<I", d, 8, 1), "size")
        self.bad(lambda d: struct.pack_into("<H", d, 16, 40), "directory")

        def sealed(fn):
            def go(d):
                fn(d)
                reseal(d)
            return go

        self.bad(sealed(lambda d: struct.pack_into("<I", d, section(d, 4) + 4, 2**31)),
                 "out of bounds")
        self.bad(sealed(lambda d: struct.pack_into("<H", d, section(d, 4) + 2, 41)), "malformed")
        self.bad(sealed(lambda d: struct.pack_into("<H", d, section(d, 7), 99)), "missing")
        self.bad(sealed(lambda d: struct.pack_into(
            "<I", d, section(d, 8) + 4, struct.unpack_from("<I", d, section(d, 2) + 4)[0])),
            "overlap")
        self.bad(lambda d: d.__setitem__(len(d) - 1, d[-1] ^ 1), "checksum mismatch")
        soff = struct.unpack_from("<I", self.data, section(self.data, 2) + 4)[0]
        self.bad(lambda d: d.__setitem__(soff, d[soff] ^ 1), "string pool", verify=True)
        ooff = struct.unpack_from("<I", self.data, section(self.data, 4) + 4)[0]
        ioff = struct.unpack_from("<I", self.data, section(self.data, 5) + 4)[0]
        roff = struct.unpack_from("<I", self.data, section(self.data, 6) + 4)[0]
        coff = struct.unpack_from("<I", self.data, section(self.data, 3) + 4)[0]
        qoff = struct.unpack_from("<I", self.data, section(self.data, 7) + 4)[0]
        self.bad(sealed(lambda d: struct.pack_into("<I", d, ooff, 2**30)), "string out")
        self.bad(sealed(lambda d: struct.pack_into("<I", d, 20, 99)), "count mismatch")
        self.bad(sealed(lambda d: d.__setitem__(ioff, 99)), "item type")
        self.bad(sealed(lambda d: struct.pack_into("<H", d, roff + 2, 999)), "relation")
        self.bad(sealed(lambda d: struct.pack_into("<H", d, ooff + 24, 999)), "object ref")
        self.bad(sealed(lambda d: struct.pack_into("<H", d, coff + 8, 999)), "category")
        self.bad(sealed(lambda d: d.__setitem__(qoff, 9)), "quiz")

    def test_validate_errors(self):
        ir = tiny_ir(2)
        ir["schema"] = "nope"
        o = ir["objects"][0]
        o["kind"], o["title"] = "nope", ""
        o["provenance"] = {}
        o["examples"] = [{"utterance": "", "benign": "x"}]
        o["relations"] = [{"type": "nope", "target": "x/1"}, {"type": "related_to",
                                                              "target": "missing"}]
        ir["objects"].append(dict(ir["objects"][1]))
        with self.assertRaises(pocket.PocketError) as cm:
            pocket.validate(ir)
        msg = str(cm.exception)
        for part in ("schema", "unknown kind", "empty title", "provenance", "example",
                     "unknown relation", "relation to unknown", "duplicate id"):
            self.assertIn(part, msg)
        with self.assertRaisesRegex(pocket.PocketError, "no objects"):
            pocket.validate({"schema": pocket.SCHEMA, "objects": []})

    def test_assessments(self):
        """A rubric becomes axis and level items and the assess capability, never a quiz."""
        ir = tiny_ir(5)
        o = ir["objects"][0]
        o["rubric"] = [{"name": "Reliability", "levels": [["A", "Sure", "very"], ["B", "Less", ""]]}]
        dump = pocket.decode(pocket.encode(ir))
        at, got = next((i, x) for i, x in enumerate(dump["objects"]) if x["id"] == "x/0")
        self.assertIn("assess", got["caps"])
        self.assertFalse([q for q in dump["quiz"]  # its trainer example stays; no term question
                          if q["kind"] == 1 and q["options"][q["answer"]] == at])
        types = [it["type"] for it in got["items"]]
        self.assertIn(pocket.ITEM_TYPES["axis"], types)
        self.assertIn({"type": pocket.ITEM_TYPES["level"], "text": "A|Sure|very"}, got["items"])
        for bad in ([{"name": "", "levels": [["A", "a", ""], ["B", "b", ""]]}],
                    [{"name": "S", "levels": [["A", "a", ""]]}],
                    [{"name": "S", "levels": [["A", "a|b", ""], ["B", "b", ""]]}],
                    [{"name": "S", "levels": [["A", "a"], ["B", "b", ""]]}]):
            o["rubric"] = bad
            with self.assertRaisesRegex(pocket.PocketError, "a scale needs"):
                pocket.validate(ir)

    def test_applications_file(self):
        with tempfile.TemporaryDirectory() as d:
            path = Path(d) / "apps.toml"
            self.assertEqual(pocket.load_applications(path), [])  # no file, no applications
            path.write_text(
                '[[application]]\nid = "applications/t"\ntitle = "T"\ndefinition = "D"\n'
                'provenance = { repository = "r", commit = "c", path = "p" }\n'
                '[[application.axis]]\nname = "S"\nlevels = [["A", "a", ""], ["B", "b", "x"]]\n')
            (o,) = pocket.load_applications(path)
            self.assertEqual(o["kind"], "protocol")
            self.assertEqual(o["category"], pocket.APPLICATIONS_CATEGORY)
            self.assertEqual(o["rubric"][0]["levels"][1], ["B", "b", "x"])
            self.assertNotIn("links", o)
        # the repository's own file, where there is one (the gate's self-test runs on a copy of
        # tools/ and tests/ only): every application validates
        own = pocket.ROOT / pocket.APPLICATIONS
        if own.exists():
            pocket.validate({"schema": pocket.SCHEMA, "objects": pocket.load_applications(own)})

    def test_simulations(self):
        """A simulation becomes its start, step and option items and the simulate capability."""
        with tempfile.TemporaryDirectory() as d:
            path = Path(d) / "scenarios.json"
            self.assertEqual(pocket.load_scenarios(path), [])  # no file, no simulations
            option = {"id": "a", "text": "Calm", "risk": "low",
                      "consequence": {"text": "Good.", "type": "success", "impact": "positive",
                                      "trust_delta": 15}}
            path.write_text(json.dumps({
                "source": {"repository": "r", "commit": "c", "path": "p"}, "start_trust": 50,
                "scenarios": [{"id": "fire_drill", "briefing": "B", "objectives": ["O"],
                               "rules": ["R"], "steps": [{"prompt": "P?",
                                                          "options": [option, option]}]}]}))
            (o,) = pocket.load_scenarios(path)
        self.assertEqual((o["id"], o["title"], o["kind"]),
                         ("simulations/fire-drill", "Fire drill", "scenario"))
        ir = tiny_ir(5)
        ir["objects"].append(o)
        dump = pocket.decode(pocket.encode(ir))
        got = next(x for x in dump["objects"] if x["id"] == o["id"])
        self.assertIn("simulate", got["caps"])
        self.assertIn({"type": pocket.ITEM_TYPES["sim_start"], "text": "50"}, got["items"])
        self.assertIn({"type": pocket.ITEM_TYPES["sim_option"],
                       "text": "low|positive|15|success|Calm|Good."}, got["items"])
        step = o["simulation"]["steps"][0]
        for broken in ({"prompt": "", "options": [option, option]},
                       {"prompt": "P", "options": [option]},
                       {"prompt": "P", "options": [option, {**option, "text": "a|b"}]},
                       {"prompt": "P", "options": [option, {**option, "consequence": {
                           **option["consequence"], "impact": "odd"}}]},
                       {"prompt": "P", "options": [option, {**option, "consequence": {
                           **option["consequence"], "trust_delta": "x"}}]}):
            o["simulation"]["steps"] = [broken]
            with self.assertRaisesRegex(pocket.PocketError, "a simulation step needs"):
                pocket.validate(ir)
        o["simulation"]["steps"] = [step]

    def test_too_many_objects(self):
        ir = tiny_ir(1)
        base = ir["objects"][0]
        ir["objects"] = [dict(base, id=f"x/{i}", relations=[]) for i in range(0xFFFF)]
        with self.assertRaisesRegex(pocket.PocketError, "65534"):
            pocket.encode(ir)


class CliTest(Fixture):
    def test_extract_reports_rejections(self):
        rc, out, err = quiet(pocket.main, ["extract", "--source", str(self.src)])
        self.assertEqual(rc, 1)
        self.assertIn("broken.md", err)

    def test_usage_unknown_and_entry_point(self):
        self.assertEqual(quiet(pocket.main, [])[0], 2)
        self.assertEqual(quiet(pocket.main, ["nope"])[0], 2)
        with mock.patch.object(sys, "argv", ["pocket.py"]):
            with self.assertRaises(SystemExit) as cm:
                quiet(runpy.run_path, str(ROOT / "tools" / "pocket.py"), None, "__main__")
        self.assertEqual(cm.exception.code, 2)

    def test_compile_refuses_invalid_ir(self):
        bad = Path(self.tmp.name) / "bad.json"
        bad.write_text(json.dumps({"schema": "x", "objects": []}))
        rc, _, err = quiet(pocket.main, ["compile", "--ir", str(bad)])
        self.assertEqual(rc, 1)
        self.assertIn("invalid IR", err)


class CliBuildTest(Fixture):
    broken = False

    def test_build_subset_decode_summary(self):
        rc, out, _ = quiet(pocket.main, ["build", "--source", str(self.src)])
        self.assertEqual(rc, 0, out)
        packs = Path(self.tmp.name) / "build" / "packs"
        pack = packs / "test.pkp"
        self.assertTrue(pack.exists())
        report = json.loads((packs / "test.report.json").read_text())
        self.assertEqual(report["objects"], 6)
        rc, out, _ = quiet(pocket.main, ["extract", "--source", str(self.src)])
        self.assertEqual(rc, 0)
        sub = Path(self.tmp.name) / "sub.json"
        rc, out, _ = quiet(pocket.main, ["subset", "--prefix", "kompendium/", "--out", str(sub)])
        self.assertEqual(rc, 0)
        self.assertTrue(all(o["id"].startswith("kompendium/")
                            for o in json.loads(sub.read_text())["objects"]))
        out_pack = Path(self.tmp.name) / "sub.pkp"
        rc, _, _ = quiet(pocket.main, ["compile", "--ir", str(sub), "--out", str(out_pack),
                                       "--report", str(Path(self.tmp.name) / "r.json")])
        self.assertEqual(rc, 0)
        rc, out, _ = quiet(pocket.main, ["decode", str(out_pack), "--strings"])
        self.assertEqual(rc, 0)
        self.assertIn('"objects"', out)
        rc, out, _ = quiet(pocket.main, ["summary", "OUN"])
        self.assertEqual(json.loads(out)["unknown"], 1)


if __name__ == "__main__":
    unittest.main()
