#!/usr/bin/env python3
"""IR -> static data for the Pages explorer, derived the same way as the device pack.

  tools/explorer.py [--ir build/packs/kompendium.ir.json] [--out build/site/data]

Writes index.json (pack metadata, stats, kinds, categories, one row per object), o/<n>.json
(one object: definition, markers, actions, confusables, chapters, examples, relations) and
quiz.json (the same questions the device derives). Text keeps its UTF-8; the browser has the
glyphs the Flipper lacks. Object order and quiz derivation are the compiler's own functions, so
object <n> here is object <n> on the device.
"""

import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import pocket  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent


def ordered(ir):
    return sorted(ir["objects"], key=lambda o: (o["category"], pocket.fold(o["title"]).lower(),
                                                o["id"]))


def build(ir, out):
    pocket.validate(ir)
    objects = ordered(ir)
    quiz = pocket.derive_quiz(objects)
    quizzed = {q["subject"] for q in quiz}
    index = {o["id"]: i for i, o in enumerate(objects)}
    cats, kinds = [], sorted({o["kind"] for o in objects})
    for o in objects:
        if not cats or cats[-1]["name"] != o["category"]:
            cats.append({"name": o["category"], "count": 0, "first": index[o["id"]]})
        cats[-1]["count"] += 1
    cat_of = {c["name"]: n for n, c in enumerate(cats)}
    rows = []
    (out / "o").mkdir(parents=True, exist_ok=True)
    for i, o in enumerate(objects):
        caps = pocket.capabilities(o, i in quizzed)
        rows.append({"t": o["title"], "k": kinds.index(o["kind"]), "c": cat_of[o["category"]],
                     "code": o.get("code", ""), "caps": caps, "m": len(o["indicators"]),
                     "a": o.get("aliases", [])[:4]})
        detail = {
            "i": i, "id": o["id"], "title": o["title"], "kind": o["kind"],
            "code": o.get("code", ""), "category": o["category"], "caps": caps,
            "definition": o["definition"], "aliases": o.get("aliases", []),
            **{role: o[role] for role in pocket.ITEM_ROLES},
            "chapters": o["chapters"], "examples": o.get("examples", []),
            "rubric": o.get("rubric", []),
            "relations": [{"type": r["type"], "to": index[r["target"]]} for r in o["relations"]],
            "provenance": o["provenance"],
        }
        (out / "o" / f"{i}.json").write_text(json.dumps(detail, ensure_ascii=False))
    counts = {k: sum(1 for o in objects if o["kind"] == k) for k in kinds}
    caps_count = {c: sum(1 for r in rows if c in r["caps"]) for c in pocket.CAPS}
    data = {"pack": ir["pack"], "kinds": kinds, "kindCounts": counts, "capCounts": caps_count,
            "categories": cats, "objects": rows,
            "relations": sum(len(o["relations"]) for o in objects)}
    (out / "index.json").write_text(json.dumps(data, ensure_ascii=False, separators=(",", ":")))
    (out / "quiz.json").write_text(json.dumps(
        [{"kind": q["kind"], "answer": q["answer"], "options": q["options"],
          "prompt": q["prompt"], "context": q["context"], "explanation": q["explanation"],
          "response": q["response"]} for q in quiz], ensure_ascii=False, separators=(",", ":")))
    return {"objects": len(objects), "categories": len(cats), "quiz": len(quiz)}


def main(argv):
    opts = dict(zip(argv[::2], argv[1::2]))
    ir = json.loads(Path(opts.get("--ir", ROOT / "build/packs/kompendium.ir.json")).read_text())
    out = Path(opts.get("--out", ROOT / "build/site/data"))
    print(json.dumps(build(ir, out)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
