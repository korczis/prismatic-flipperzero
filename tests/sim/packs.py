#!/usr/bin/env python3
"""Hermetic packs for the UI scenarios, built with the real compiler (tools/pocket.py).

  tests/sim/packs.py <out-dir>

ui.pkp: a long category (paging, list wrap), a full object (every section, chapters, both
example kinds, confusables and related links), a bare article (no links), and an object whose
chapter is too long for one page. bad.pkp: ui.pkp with one object, one item, one relation, one
category and one quiz record made invalid and the index checksum resealed, so the pack opens
and the app meets each broken record in turn.
"""

import struct
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import pocket  # noqa: E402


def obj(oid, title, cat, kind="technique", **kw):
    o = {"id": oid, "kind": kind, "title": title, "code": kw.pop("code", ""), "category": cat,
         "definition": kw.pop("definition", f"{title} is defined here for the test."),
         "indicators": [], "actions": [], "escalation": [], "alternatives": [], "validation": [],
         "chapters": [], "examples": [], "relations": [],
         "provenance": {"repository": "r", "commit": "c", "path": f"{oid}.md"}}
    o.update(kw)
    return o


def ir():
    objects = [obj(f"long/{i:02d}", f"Long entry {i:02d}", "A long category", code=f"L-{i}",
                   indicators=[f"marker {i}"],
                   definition=f"Long entry {i:02d} has a definition long enough for a quiz "
                              f"question." if i % 3 == 0 else f"Entry {i}.")
               for i in range(45)]
    objects.append(obj("spaced", "a b c d e f g h i j k l m n o p q r s t u v w x y z a b c d",
                       "A long category"))
    objects.append(obj(
        "full", "Full object with every section", "B short", code="F-1",
        indicators=["first marker that is long enough to wrap onto a second line", "second",
                    "third"],
        actions=["act now"], escalation=["escalate"], alternatives=["an honest mistake"],
        validation=["ask for the source"],
        chapters=[{"heading": "Chapter one", "text": "First chapter text."},
                  {"heading": "Chapter two", "text": "Second chapter text."}],
        examples=[{"utterance": "You always forget.", "context": "At work", "benign": False,
                   "marker": "generalisation", "response": "Name one time."},
                  {"utterance": "Could you check this?", "context": "At home", "benign": True,
                   "marker": "request", "response": "Sure."}],
        relations=[{"type": "confused_with", "target": "bare"},
                   {"type": "related_to", "target": "huge"}]))
    objects.append(obj("bare", "Bare article", "B short", kind="article", definition="Short."))
    objects.append(obj(
        "hub", "Hub of links", "B short", kind="concept",
        definition="The hub links to many entries. It exists to test the map. A map shows four "
                   "neighbours at a time. More neighbours rotate into view. The last sentence "
                   "ends here.",
        relations=[{"type": "confused_with", "target": f"long/{i:02d}"} for i in range(3)] +
                  [{"type": "related_to", "target": f"long/{i:02d}"} for i in range(3, 9)]))
    objects.append(obj("huge", "Huge chapter", "B short", kind="concept",
                       chapters=[{"heading": "Everything", "text": "word " * 2600}] +
                       [{"heading": f"Part {k}", "text": "x"} for k in range(240)],
                       relations=[{"type": "related_to", "target": "full"}]))
    # a pair for side-by-side detection: its look-alike (full) has markers of its own; its own
    # category sorts last, so the other lists keep their rows
    objects.append(obj("pair", "Pair with the full object", "C compare",
                       indicators=["pair marker one", "pair marker two"], definition="Short.",
                       relations=[{"type": "confused_with", "target": "full"}]))
    # an assessment: two scales, a grade with a meaning and one without, guidance and rules
    objects.append(obj("rate", "Rate a source", "D assess", kind="protocol", code="R",
                       definition="Short.", actions=["use the grade"], validation=["grade fairly"],
                       rubric=[{"name": "Axis one", "levels": [["A", "Alpha", "first meaning"],
                                                               ["B", "Beta", ""]]},
                               {"name": "Axis two", "levels": [["1", "One", ""],
                                                               ["2", "Two", "second"],
                                                               ["3", "Three", ""]]}]))
    # a simulation whose picks reach every outcome: A,C optimal; A,E acceptable; A,D suboptimal;
    # B,D critical failure
    def op(oid, text, impact, delta, kind="info"):
        return {"id": oid, "text": text, "risk": "low",
                "consequence": {"text": f"after {text}", "type": kind, "impact": impact,
                                "trust_delta": delta}}
    objects.append(obj("drill", "Drill", "E simulate", kind="scenario", definition="Short.",
                       simulation={"start_trust": 50, "steps": [
                           {"prompt": "First step?", "options": [
                               op("a", "Opt A", "positive", 25, "success"),
                               op("b", "Opt B", "negative", -30, "danger")]},
                           {"prompt": "Second step?", "options": [
                               op("c", "Opt C", "positive", 10),
                               op("d", "Opt D", "critical", -40, "danger"),
                               op("e", "Opt E", "neutral", -10)]}]}))
    # a scorecard: base 50, a question raising it, one lowering it, three bands
    objects.append(obj("card", "Card", "F score", kind="protocol", definition="Short.",
                       actions=["act on it"], validation=["score fairly"],
                       scorecard={"base": 50, "min": 0, "max": 100,
                                  "questions": [
                                      {"text": "Raise?", "choices": [["No", 0, ""],
                                                                     ["Yes", 40, "raised a lot"]]},
                                      {"text": "Lower?", "choices": [["No", 0, ""],
                                                                     ["Yes", -60, ""]]}],
                                  "bands": [[30, "LO", "Low", "below thirty"],
                                            [70, "MID", "Middle", ""],
                                            [100, "HI", "High", "above seventy"]]}))
    # a calculator, in the last category after the card so the card stays first there
    objects.append(obj("zcalc", "Zeta calculator", "F score", kind="algorithm",
                       definition="Short.",
                       calculator={"algorithm": "bayes", "prior": 50, "confirm": 90, "reject": 10,
                                   "choices": [10, 90]}))
    return {"schema": pocket.SCHEMA,
            "pack": {"id": "ui", "title": "UI test pack", "version": "1",
                     "source_repository": "r", "source_commit": "c"},
            "objects": objects}


def section(d, kind):
    """(entry size, offset, length, count) of a section, from the directory."""
    for k in range(struct.unpack_from("<H", d, 16)[0]):
        t, size, off, length, count = struct.unpack_from("<HHIII", d, 32 + 16 * k)
        if t == kind:
            return size, off, length, count
    raise KeyError(kind)


def reseal(d):
    """The index CRC covers everything after the header except the string pool."""
    _size, so, sl, _count = section(d, 2)
    d[12:16] = struct.pack("<I", zlib.crc32(bytes(d[so + sl:]), zlib.crc32(bytes(d[32:so]))))


def corrupt(data, titles, full_quiz, confused):
    d = bytearray(data)
    size, objs, _len, _n = section(d, 4)  # OBJECTS: kind +26, items_first +28, rel_first +34
    d[objs + size * titles.index("Long entry 01") + 26] = 0
    full = objs + size * titles.index("Full object with every section")
    items_first, rel_first = struct.unpack_from("<I", d, full + 28)[0], \
        struct.unpack_from("<I", d, full + 34)[0]
    size, off, _len, _n = section(d, 5)  # ITEMS: type +0
    d[off + size * (items_first + 1)] = 0  # its second marker
    size, off, _len, _n = section(d, 6)  # RELATIONS: type +0
    d[off + size * rel_first] = 0
    size, off, _len, count = section(d, 3)  # CATEGORIES: count u16 +8
    struct.pack_into("<H", d, off + size * (count - 1) + 8, 0xFFFF)
    size, off, _len, _n = section(d, 7)  # QUIZ: first option +2, of the full object's question
    struct.pack_into("<H", d, off + size * full_quiz + 2, 0xFFFF)
    size, off, _len, _n = section(d, 9)  # RELNAMES: confused_with's name becomes empty
    struct.pack_into("<H", d, off + size * confused + 4, 0)
    reseal(d)
    return bytes(d)


def main(argv):
    out = Path(argv[0])
    out.mkdir(parents=True, exist_ok=True)
    data = pocket.encode(ir())
    (out / "ui.pkp").write_bytes(data)
    dump = pocket.decode(data)
    titles = [o["title"] for o in dump["objects"]]
    full = titles.index("Full object with every section")
    full_quiz = next(k for k, q in enumerate(dump["quiz"])
                     if q["kind"] == 1 and q["options"][q["answer"]] == full)
    confused = pocket.RELATIONS.index("confused_with")
    (out / "bad.pkp").write_bytes(corrupt(data, titles, full_quiz, confused))
    print(f"packs: {out}/ui.pkp {len(data)} B, {out}/bad.pkp")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
