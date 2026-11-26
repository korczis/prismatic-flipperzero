#!/usr/bin/env python3
"""Prismatic Pocket knowledge compiler: Kompendium sources -> canonical IR -> .pkp pack.

Pipeline: discover -> extract -> normalize -> merge -> resolve -> validate -> derive -> encode.

  tools/pocket.py extract [--source DIR] [--out F]   pinned sources -> IR JSON + report
  tools/pocket.py compile [--ir F] [--out F] [--report F]   IR -> pack (+ build report)
  tools/pocket.py build   [--source DIR]             extract + compile into build/packs/
  tools/pocket.py subset  --prefix P --out F [--ir F]   IR of the objects whose id starts with P
  tools/pocket.py decode  <pack> [--strings]         reference reader: canonical dump (JSON)
  tools/pocket.py summary <answers>                  reference detection summary, e.g. OONU?

Everything is driven by knowledge/sources.json: which pages, which collection is which kind,
which heading plays which role (definition, indicators, actions, escalation, alternatives,
validation), which overlays merge (quick-reference cards into techniques, catalogue entries
into their pages). Sources are read with `git show <commit>:<path>` at the pinned commit, so
the working tree of the source checkout never leaks in. The IR keeps UTF-8; the pack carries an
ASCII fold, because the Flipper's built-in fonts render no glyph outside ASCII (measured:
docs/evidence/fonts). The IR and the pack are build outputs, never committed.
"""

import hashlib
import io
import json
import os
import re
import struct
import subprocess
import sys
import tarfile
import tomllib
import unicodedata
import zlib
from pathlib import Path, PurePosixPath

ROOT = Path(__file__).resolve().parent.parent
SOURCES = ROOT / "knowledge" / "sources.json"
SCHEMA = "prismatic.knowledge/v1"
FORMAT_VERSION = 1
MAGIC = b"PKP1"
HEADER = struct.Struct("<4sHHIIHHIII")  # 32 bytes
SECTION = struct.Struct("<HHIII")  # type, entry_size, offset, length, count
STR = struct.Struct("<IH")
OBJECT = struct.Struct("<IHIHIHIHHBBIHIH")  # 40 bytes
ITEM = struct.Struct("<BBIH")  # 8 bytes
RELATION = struct.Struct("<BBH")  # 4 bytes
CATEGORY = struct.Struct("<IHHH")  # 10 bytes
QUIZ = struct.Struct("<BBHHHHIHIHIHIH")  # 34 bytes
(SEC_META, SEC_STRINGS, SEC_CATEGORIES, SEC_OBJECTS, SEC_ITEMS, SEC_RELATIONS, SEC_QUIZ,
 SEC_KINDS, SEC_RELNAMES) = range(1, 10)
ENTRY_SIZE = {SEC_META: STR.size, SEC_STRINGS: 1, SEC_CATEGORIES: CATEGORY.size,
              SEC_OBJECTS: OBJECT.size, SEC_ITEMS: ITEM.size, SEC_RELATIONS: RELATION.size,
              SEC_QUIZ: QUIZ.size, SEC_KINDS: STR.size, SEC_RELNAMES: STR.size}
KINDS = ["pattern", "technique", "signature", "fallacy", "bias", "trait", "modality",
         "heuristic", "protocol", "countermeasure", "exercise", "algorithm", "scenario",
         "concept", "article"]
ITEM_ROLES = ["indicators", "actions", "escalation", "alternatives", "validation"]
ITEM_TYPES = {"indicators": 1, "actions": 2, "escalation": 3, "alternatives": 4,
              "validation": 5, "heading": 6, "text": 7, "example": 8, "axis": 9, "level": 10,
              "sim_start": 11, "sim_step": 12, "sim_option": 13, "score_setup": 14,
              "score_question": 15, "score_choice": 16, "score_band": 17, "calc": 18}
RELATIONS = ["related_to", "contrasts_with", "confused_with", "specializes", "generalizes",
             "requires", "supports", "contradicts", "mitigated_by", "illustrated_by"]
CAPS = {"browse": 1, "detect": 2, "apply": 4, "quiz": 8, "compare": 16, "assess": 32,
        "simulate": 64, "score": 128}
SCENARIOS = Path("knowledge") / "scenarios.json"  # under ROOT
SIMULATIONS_CATEGORY = "Simulations"
SIM_IMPACTS = ("positive", "neutral", "negative", "critical")
CALCULATORS = ("bayes",)  # algorithms the device implements (app/core/bayes.c)
APPLICATIONS = Path("knowledge") / "applications.toml"  # under ROOT
WORKING_FIELDS = ("links", "confused_links", "related_terms", "catalog_id", "ea_code")
APPLICATIONS_CATEGORY = "Applications"
META_FIELDS = ["pack_id", "title", "version", "source_repository", "source_commit",
               "generator", "licence"]
QUIZ_DEFINITION, QUIZ_TRAINER = 1, 2
MAX_STR = 2048
MAX_DEFINITION = 900
CHUNK = 1400


class PocketError(Exception):
    """A source, IR or pack that the compiler refuses, with the reason."""


# ---------------------------------------------------------------- discover


class Git:
    """Files of one commit, read once with `git archive`; nothing from the working tree."""

    def __init__(self, source, commit, prefixes):
        tar = subprocess.run(["git", "-C", str(source), "archive", commit, *prefixes],
                             check=True, capture_output=True).stdout
        with tarfile.open(fileobj=io.BytesIO(tar)) as t:
            self.blobs = {m.name: t.extractfile(m).read().decode("utf-8")
                          for m in t.getmembers() if m.isfile()}

    def files(self, prefix):
        root = prefix.rstrip("/") + "/"
        return sorted(name for name in self.blobs if name.startswith(root))

    def read(self, path):
        return self.blobs[path]


# ---------------------------------------------------------------- markdown


def split_frontmatter(text):
    m = re.match(r"\+\+\+\n(.*?)\n\+\+\+\n?(.*)", text, re.S)
    if not m:
        raise PocketError("no TOML front matter between +++ lines")
    return tomllib.loads(m.group(1)), m.group(2)


def sections(body):
    """[(heading, text)] in order; text before the first '## ' has heading ''."""
    out, current, lines = [], "", []
    for line in body.splitlines():
        if line.startswith("## "):
            out.append((current, "\n".join(lines).strip()))
            current, lines = line[3:].strip(), []
        else:
            lines.append(line)
    out.append((current, "\n".join(lines).strip()))
    return [(h, t) for h, t in out if h or t]


def plain(md):
    """Markdown inline -> plain text: links to their text, emphasis and code marks dropped."""
    text = re.sub(r"\[([^\]]*)\]\([^)]*\)", r"\1", md)
    text = re.sub(r"(\*\*|__|`|^#+\s*)", "", text, flags=re.M)
    text = re.sub(r"(?<!\w)\*(?!\s)([^*]+)(?<!\s)\*(?!\w)", r"\1", text)
    return re.sub(r"\s+", " ", text).strip()


def blocks(md):
    """Split a section into ('bullet'|'row'|'para', text) blocks, tables as data rows."""
    out, para = [], []

    def flush():
        if para:
            out.append(("para", plain(" ".join(para))))
            para.clear()

    rows_seen = 0
    for line in md.splitlines():
        s = line.strip()
        if re.match(r"^[-*] ", s) or re.match(r"^\d+\. ", s):
            flush()
            out.append(("bullet", plain(re.sub(r"^([-*]|\d+\.) ", "", s))))
        elif s.startswith("|"):
            flush()
            cells = [c.strip() for c in s.strip("|").split("|")]
            if all(re.fullmatch(r":?-{2,}:?", c) for c in cells if c):
                continue
            rows_seen += 1
            if rows_seen == 1:
                continue  # header row
            out.append(("row", plain(" - ".join(c for c in cells if c))))
        else:
            rows_seen = 0
            if not s or s.startswith("#"):
                flush()
            else:
                para.append(s)
    flush()
    return [(k, t) for k, t in out if t]


def items_of(md):
    """List items of a role section: bullets and table rows when present, else paragraphs.

    A paragraph that ends in ':' and is followed by another introduces what follows ("Technika
    je primarne indikovana v nasledujicich klinickych kontextech:"); it is a lead-in, not an
    item, so a detector never asks it as a marker."""
    bl = blocks(md)
    listed = [t for k, t in bl if k in ("bullet", "row")]
    if listed:
        return listed
    paras = [t for k, t in bl if k == "para"]
    return [t for i, t in enumerate(paras) if not (t.rstrip().endswith(":") and i < len(paras) - 1)]


def chunks(text, size=CHUNK):
    """Split long text at sentence ends into pieces of at most `size` characters."""
    out, cur = [], ""
    for sentence in re.split(r"(?<=[.!?:;])\s+", text):
        while len(sentence) > size:
            if cur:
                out.append(cur)
                cur = ""
            out.append(sentence[:size])
            sentence = sentence[size:]
        if cur and len(cur) + len(sentence) + 1 > size:
            out.append(cur)
            cur = sentence
        else:
            cur = f"{cur} {sentence}".strip()
    if cur:
        out.append(cur)
    return out


LINK = re.compile(r"\]\(@/([^)#]+?\.md)(?:#[^)]*)?\)")


def links(md):
    return ["content/" + p for p in LINK.findall(md)]


# ---------------------------------------------------------------- extract


def norm(text):
    return re.sub(r"\s+", " ", text.strip().lower())


def role_of(heading, roles):
    h = norm(heading)
    for role, prefixes in roles.items():
        if any(h.startswith(norm(p)) for p in prefixes):
            return role
    return None


def collection_of(path, roots):
    rel = PurePosixPath(path)
    for root in roots:
        r = PurePosixPath(root)
        if rel.is_relative_to(r):
            parts = rel.relative_to(r).parts
            return r.name if len(parts) == 1 else parts[0]
    raise PocketError(f"{path}: outside the configured roots")


def page_id(path):
    return str(PurePosixPath(path).with_suffix("")).removeprefix("content/")


def empty_object(oid, kind, title, category, prov):
    return {"schema": SCHEMA, "id": oid, "kind": kind, "title": title, "code": "",
            "category": category, "tags": [], "aliases": [], "definition": "",
            **{r: [] for r in ITEM_ROLES}, "chapters": [], "examples": [], "links": [],
            "confused_links": [], "related_terms": [], "catalog_id": "", "ea_code": "",
            "relations": [], "validation_status": "unvalidated_heuristic", "provenance": prov}


def extract_page(text, path, spec, titles, prov):
    front, body = split_frontmatter(text)
    extra = front.get("extra", {})
    coll = collection_of(path, spec["pages"]["roots"])
    kind = spec["kinds"].get(coll, spec["kinds"]["default"])
    title = front.get("title", PurePosixPath(path).stem)
    for pattern in spec.get("title_strip", []):
        title = re.sub(pattern, "", title)
    parent = PurePosixPath(path).parent
    category = titles.get(str(parent)) or titles.get(str(parent.parent)) or coll
    o = empty_object(page_id(path), kind, title.strip(), category, {**prov, "path": path})
    o["ea_code"] = str(extra.get("ea_code", ""))
    o["catalog_id"] = str(extra.get("catalog_id", ""))
    o["code"] = o["ea_code"] or o["catalog_id"]
    o["tags"] = sorted(set(front.get("taxonomies", {}).get("tags", [])) - set(spec["drop_tags"]))
    o["aliases"] = [k for k in extra.get("keywords", []) if norm(k) != norm(o["title"])][:8]
    o["related_terms"] = [str(t) for t in extra.get("related_terms", [])]
    definition = ""
    for heading, md in sections(body):
        role = role_of(heading, spec["roles"]) if heading else None
        o["links"] += links(md)
        if role == "skip":
            continue
        if role == "definition":
            first = blocks(md)
            definition = definition or (first[0][1] if first else "")
            if len(plain(md)) > len(definition) + 40:
                o["chapters"].append({"heading": heading, "text": plain(md)})
            continue
        if role in ITEM_ROLES:
            o[role] += items_of(md)
            if role == "alternatives":
                o["confused_links"] += links(md)
            continue
        text = plain(md)
        if text and (heading or len(text) > 160):
            o["chapters"].append({"heading": heading or o["title"], "text": text})
    o["definition"] = definition or plain(front.get("description", "")) or (
        o["chapters"][0]["text"] if o["chapters"] else o["title"])
    return o


def catalog_entries(git, spec):
    for cat in spec["catalogs"]:
        for path in git.files(cat["path"]):
            if not re.search(cat["include"], PurePosixPath(path).name):
                continue
            for entry in tomllib.loads(git.read(path)).get("pattern", []):
                yield cat, path, entry


def merge_catalog(objects, git, spec, prov, report):
    by_catalog = {o["catalog_id"]: o for o in objects.values() if o["catalog_id"]}
    by_path = {o["provenance"]["path"]: o for o in objects.values()}
    slugs = {}
    for cat, path, e in catalog_entries(git, spec):
        report["catalog_entries"] += 1
        target = by_catalog.get(e["id"]) or by_path.get("content/" + e.get("canonical", ""))
        if target is None:
            oid = f"{cat['prefix']}/{e['slug']}"
            target = objects.get(oid)
            if target is None:
                target = empty_object(oid, cat["kind"], e["cs"], cat["category"],
                                      {**prov, "path": path})
                target["definition"] = f"{e['cs']} ({e.get('en', '')})."
                objects[oid] = target
                report["catalog_standalone"] += 1
            target["links"] += ["content/" + k for k in e.get("kompendium", [])]
        else:
            report["catalog_merged"] += 1
        target["code"] = target["code"] or e["id"]
        names = [e.get("en", "")] + e.get("aliases", [])
        target["aliases"] = list(dict.fromkeys(target["aliases"] + [n for n in names if n]))[:10]
        target["related_terms"] += e.get("related", [])
        slugs[e["slug"]] = target["id"]
        for t in e.get("trainer", []):
            target["examples"].append({
                "utterance": t["utterance"], "context": t.get("context", ""),
                "benign": bool(t["benign"]), "marker": t.get("marker", ""),
                "response": t.get("response", ""), "review": e.get("review", "")})
            report["trainer_items"] += 1
    return slugs


def overlay_quick_reference(objects, spec, report):
    """Merge each quick-reference card into the technique with the same EA code."""
    prefix = spec["overlay"]["from_prefix"]
    by_code = {o["ea_code"]: o for o in objects.values()
               if o["ea_code"] and not o["id"].startswith(prefix)}
    aliases = {}
    for oid in sorted(o for o in objects if o.startswith(prefix)):
        card = objects[oid]
        target = by_code.get(card["ea_code"])
        if target is None:
            continue
        for role in ITEM_ROLES:
            if card[role]:
                target[role] = card[role]  # the curated card's lists replace prose-derived ones
        target["definition"] = card["definition"]
        for k in ("links", "confused_links", "related_terms"):
            target[k] += card[k]
        aliases[oid] = target["id"]
        del objects[oid]
        report["overlays_merged"] += 1
    return aliases


def resolve(objects, aliases, slugs, report):
    by_path = {o["provenance"]["path"]: o["id"] for o in objects.values()}
    by_path.update({"content/" + a + ".md": t for a, t in aliases.items()})
    by_slug = {}
    for oid in list(objects) + list(aliases):
        by_slug.setdefault(PurePosixPath(oid).name, set()).add(aliases.get(oid, oid))
    for slug, oid in slugs.items():
        by_slug.setdefault(slug, set()).add(oid)

    def find(ref):
        if ref in by_path:
            return by_path[ref]
        hits = by_slug.get(PurePosixPath(ref).stem, set())
        return next(iter(hits)) if len(hits) == 1 else None

    for o in objects.values():
        rels, seen = [], set()
        confused = {find(p) for p in o["confused_links"]}
        for kind, refs in (("confused_with", o["confused_links"]),
                           ("related_to", o["links"] + o["related_terms"])):
            for ref in refs:
                target = find(ref)
                if target is None:
                    report["unresolved_references"] += 1
                    continue
                if target == o["id"] or target in seen or (
                        kind == "related_to" and target in confused):
                    continue
                seen.add(target)
                rels.append({"type": kind, "target": target})
        o["relations"] = rels[:24]
        for k in WORKING_FIELDS:
            del o[k]


def category_titles(git, spec):
    titles = {}
    for root in spec["pages"]["roots"]:
        for path in git.files(root):
            if PurePosixPath(path).name == "_index.md":
                front, _ = split_frontmatter(git.read(path))
                titles[str(PurePosixPath(path).parent)] = front.get("title", "")
    return titles


def load_applications(path=None):
    """The assessments in knowledge/applications.toml as IR objects with a `rubric`: their
    scales and grades, with each one's own provenance (they are not pages of the book)."""
    path = Path(path) if path else ROOT / APPLICATIONS
    if not path.exists():
        return []
    data = tomllib.loads(path.read_text())
    objects = []
    for a in data.get("application", []):
        o = empty_object(a["id"], "protocol", a["title"], APPLICATIONS_CATEGORY, a["provenance"])
        o.update(code=a.get("code", ""), definition=a["definition"], actions=a.get("actions", []),
                 validation=a.get("validation", []), validation_status="sourced_scale",
                 rubric=[{"name": x["name"], "levels": [list(lv) for lv in x["levels"]]}
                         for x in a["axis"]])
        for k in WORKING_FIELDS:  # an application has no page links to resolve
            del o[k]
        objects.append(o)
    for a in data.get("calculator", []):
        o = empty_object(a["id"], "algorithm", a["title"], APPLICATIONS_CATEGORY, a["provenance"])
        o.update(code=a.get("code", ""), definition=a["definition"], actions=a.get("actions", []),
                 validation=a.get("validation", []), validation_status="sourced_algorithm",
                 calculator={k: a[k] for k in ("algorithm", "prior", "confirm", "reject",
                                               "choices")})
        for k in WORKING_FIELDS:
            del o[k]
        objects.append(o)
    for a in data.get("scorecard", []):
        o = empty_object(a["id"], "protocol", a["title"], APPLICATIONS_CATEGORY, a["provenance"])
        o.update(code=a.get("code", ""), definition=a["definition"], actions=a.get("actions", []),
                 validation=a.get("validation", []), validation_status="sourced_scorecard",
                 scorecard={"base": a["base"], "min": a["min"], "max": a["max"],
                            "questions": [{"text": q["text"],
                                           "choices": [list(c) for c in q["choices"]]}
                                          for q in a["question"]],
                            "bands": [list(b) for b in a["bands"]]})
        for k in WORKING_FIELDS:
            del o[k]
        objects.append(o)
    return objects


def load_scenarios(path=None):
    """The crisis simulations in knowledge/scenarios.json (imported from the Prismatic Platform
    by tools/prismatic_import.py) as IR objects with a `simulation`: steps, options and their
    consequences, and the trust a run starts with."""
    path = Path(path) if path else ROOT / SCENARIOS
    if not path.exists():
        return []
    data = json.loads(path.read_text())
    prov = data["source"]
    objects = []
    for sc in data["scenarios"]:
        title = sc["id"].replace("_", " ").capitalize()
        o = empty_object(f"simulations/{sc['id'].replace('_', '-')}", "scenario", title,
                         SIMULATIONS_CATEGORY, prov)
        o.update(code="SIM", definition=sc["briefing"], validation_status="sourced_simulation",
                 chapters=[{"heading": "Objectives", "text": "; ".join(sc["objectives"]) + "."},
                           {"heading": "Rules", "text": "; ".join(sc["rules"]) + "."}],
                 simulation={"start_trust": data["start_trust"], "steps": sc["steps"]})
        for k in WORKING_FIELDS:
            del o[k]
        objects.append(o)
    return objects


def extract(source_dir):
    spec = json.loads(SOURCES.read_text())
    src = spec["source"]
    git = Git(source_dir, src["commit"],
              spec["pages"]["roots"] + [c["path"] for c in spec["catalogs"]])
    prov = {"repository": src["repository"], "commit": src["commit"]}
    report = {"pages": 0, "accepted": 0, "rejected": [], "overlays_merged": 0,
              "catalog_entries": 0, "catalog_merged": 0, "catalog_standalone": 0,
              "trainer_items": 0, "unresolved_references": 0, "duplicates": 0}
    titles = category_titles(git, spec)
    objects = {}
    for root in spec["pages"]["roots"]:
        for path in git.files(root):
            name = PurePosixPath(path).name
            if not name.endswith(".md") or name in spec["pages"]["exclude"]:
                continue
            report["pages"] += 1
            try:
                o = extract_page(git.read(path), path, spec, titles, prov)
            except (PocketError, tomllib.TOMLDecodeError) as e:
                report["rejected"].append(f"{path}: {e}")
                continue
            objects[o["id"]] = o
    aliases = overlay_quick_reference(objects, spec, report)
    slugs = merge_catalog(objects, git, spec, prov, report)
    resolve(objects, aliases, slugs, report)
    for o in load_applications() + load_scenarios():
        objects[o["id"]] = o
    report["applications"] = sum(1 for o in objects.values() if o.get("rubric"))
    report["simulations"] = sum(1 for o in objects.values() if o.get("simulation"))
    report["scorecards"] = sum(1 for o in objects.values() if o.get("scorecard"))
    report["accepted"] = len(objects)
    ir = {"schema": SCHEMA, "pack": {**spec["pack"], "source_repository": src["repository"],
                                     "source_commit": src["commit"]},
          "objects": [objects[k] for k in sorted(objects)]}
    return ir, report


# ---------------------------------------------------------------- validate / derive


def validate(ir):
    errors = []
    if ir.get("schema") != SCHEMA:
        errors.append(f"schema must be {SCHEMA}")
    seen = set()
    for o in ir.get("objects", []):
        oid = o.get("id", "?")
        if oid in seen:
            errors.append(f"{oid}: duplicate id")
        seen.add(oid)
        if o.get("kind") not in KINDS:
            errors.append(f"{oid}: unknown kind {o.get('kind')!r}")
        for field in ("title", "category"):
            if not o.get(field):
                errors.append(f"{oid}: empty {field}")
        prov = o.get("provenance", {})
        if not all(prov.get(k) for k in ("repository", "commit", "path")):
            errors.append(f"{oid}: provenance needs repository, commit and path")
        for ex in o.get("examples", []):
            if not ex.get("utterance") or not isinstance(ex.get("benign"), bool):
                errors.append(f"{oid}: example needs utterance and boolean benign")
        for axis in o.get("rubric", []):
            levels = axis.get("levels", [])
            if not axis.get("name") or len(levels) < 2 or any(
                    len(lv) != 3 or not lv[0] or not lv[1] or "|" in "".join(lv)
                    for lv in levels):
                errors.append(f"{oid}: a scale needs a name and 2+ [code, label, meaning]")
        card = o.get("scorecard")
        if card:
            bands, qs = card.get("bands", []), card.get("questions", [])
            texts = [str(x) for q in qs for c in q.get("choices", []) for x in c] + \
                [str(x) for b in bands for x in b]
            if (not qs or not bands or bands[-1][0] < card["max"]
                    or any(len(c) != 3 or not isinstance(c[1], int) for q in qs
                           for c in q.get("choices", []))
                    or any(not 2 <= len(q.get("choices", [])) <= 6 for q in qs)
                    or any("|" in t for t in texts)
                    or [b[0] for b in bands] != sorted(b[0] for b in bands)):
                errors.append(f"{oid}: a scorecard needs questions of 2-6 [label, weight, note] "
                              "and ascending bands up to its max")
        calc = o.get("calculator")
        if calc and (calc.get("algorithm") not in CALCULATORS or not all(
                isinstance(v, int) and 0 < v < 100
                for v in [calc["prior"], calc["confirm"], calc["reject"], *calc["choices"]])
                or not 1 <= len(calc["choices"]) <= 8):
            errors.append(f"{oid}: a calculator needs a known algorithm and percentages 1-99")
        sim = o.get("simulation")
        for step in (sim or {}).get("steps", []):
            ops = step.get("options", [])
            if not step.get("prompt") or not 2 <= len(ops) <= 8 or any(
                    op["consequence"]["impact"] not in SIM_IMPACTS
                    or not isinstance(op["consequence"]["trust_delta"], int)
                    or "|" in op["text"] + op["consequence"]["text"] + op["consequence"]["type"]
                    for op in ops):
                errors.append(f"{oid}: a simulation step needs a prompt and 2-8 valid options")
    for o in ir.get("objects", []):
        for r in o.get("relations", []):
            if r.get("type") not in RELATIONS:
                errors.append(f"{o['id']}: unknown relation {r.get('type')!r}")
            elif r.get("target") not in seen:
                errors.append(f"{o['id']}: relation to unknown {r.get('target')!r}")
    if not ir.get("objects"):
        errors.append("no objects")
    if errors:
        raise PocketError("invalid IR:\n  " + "\n  ".join(errors[:50]))


def capabilities(o, has_quiz):
    caps = ["browse"]
    if o["indicators"]:
        caps.append("detect")
    if o["actions"] or o["escalation"]:
        caps.append("apply")
    if has_quiz:
        caps.append("quiz")
    if o["alternatives"] or any(r["type"] == "confused_with" for r in o["relations"]):
        caps.append("compare")
    if o.get("rubric"):
        caps.append("assess")
    if o.get("simulation"):
        caps.append("simulate")
    if o.get("scorecard"):
        caps.append("score")
    return caps


def mask(text, title):
    """Hide the answer: the title and each of its words of 5+ letters (with any ending)."""
    out = re.sub(re.escape(title), "___", text, flags=re.I)
    for word in sorted(set(re.findall(r"\w{5,}", title)), key=len, reverse=True):
        out = re.sub(rf"\b{re.escape(word[:-1])}\w*", "___", out, flags=re.I)
    return out


def derive_quiz(objects):
    """Definition recognition for every non-article object with a real definition (not the
    assessments and simulations: they are tools, not terms), and one
    trainer question per catalogue example. Distractors come from the subject's own relations
    first (confusable by construction), then its category, then the pack, each in id order;
    the answer position is a deterministic function of the id, never a random seed."""
    index = {o["id"]: i for i, o in enumerate(objects)}
    by_cat = {}
    for o in objects:
        by_cat.setdefault(o["category"], []).append(o["id"])
    ids = [o["id"] for o in objects]
    quiz = []
    for i, o in enumerate(objects):
        if (len(o["definition"]) >= 60 and o["kind"] != "article" and not o.get("rubric")
                and not o.get("simulation") and not o.get("scorecard")
                and not o.get("calculator")):
            pool = [r["target"] for r in o["relations"]] + by_cat[o["category"]] + ids
            picks, titles = [], {fold(o["title"]).lower()}
            for cand in pool:
                t = fold(objects[index[cand]]["title"]).lower()
                if t not in titles:
                    picks.append(cand)
                    titles.add(t)
                if len(picks) == 3:
                    break
            if len(picks) == 3:
                pos = int(hashlib.sha256(o["id"].encode()).hexdigest(), 16) % 4
                options = [index[p] for p in picks]
                options.insert(pos, i)
                quiz.append({"kind": QUIZ_DEFINITION, "answer": pos, "options": options,
                             "prompt": clip(mask(o["definition"], o["title"]), 600),
                             "context": "", "explanation": "", "response": "", "subject": i})
        for ex in o["examples"]:
            quiz.append({"kind": QUIZ_TRAINER, "answer": 1 if ex["benign"] else 0,
                         "options": [i, i, i, i], "prompt": ex["utterance"],
                         "context": ex["context"], "explanation": ex["marker"],
                         "response": ex["response"], "subject": i})
    return quiz


# ---------------------------------------------------------------- encode / decode

FOLD = {"„": '"', "“": '"', "”": '"', "‚": "'", "‘": "'", "’": "'", "–": "-", "—": "-",
        "…": "...", "·": "-", "×": "x", "→": "->", "←": "<-", "≈": "~", " ": " ",
        "≥": ">=", "≤": "<=", "≠": "!=", "«": '"', "»": '"', "•": "-"}


def fold(text):
    """UTF-8 -> printable ASCII for the device fonts; anything unmappable becomes '?'."""
    text = "".join(FOLD.get(ch, ch) for ch in text)
    text = unicodedata.normalize("NFKD", text)
    text = "".join(ch for ch in text if not unicodedata.combining(ch))
    return "".join(ch if 32 <= ord(ch) < 127 else "?" for ch in text)


def clip(text, limit):
    return text if len(text) <= limit else text[: limit - 3].rstrip() + "..."


class Strings:
    def __init__(self):
        self.pool, self.at = bytearray(), {}

    def ref(self, text, limit=MAX_STR):
        data = clip(fold(text), limit).encode("ascii")
        if data not in self.at:
            self.at[data] = len(self.pool)
            self.pool += data
        return self.at[data], len(data)


def object_items(o):
    for role in ITEM_ROLES:
        for text in o[role]:
            yield ITEM_TYPES[role], text
    sim = o.get("simulation")
    if sim:
        yield ITEM_TYPES["sim_start"], str(sim["start_trust"])
        for step in sim["steps"]:
            yield ITEM_TYPES["sim_step"], step["prompt"]
            for op in step["options"]:
                c = op["consequence"]
                yield ITEM_TYPES["sim_option"], "|".join(
                    [op["risk"], c["impact"], str(c["trust_delta"]), c["type"], op["text"],
                     c["text"]])
    card = o.get("scorecard")
    if card:
        yield ITEM_TYPES["score_setup"], f"{card['base']}|{card['min']}|{card['max']}"
        for q in card["questions"]:
            yield ITEM_TYPES["score_question"], q["text"]
            for label, weight, note in q["choices"]:
                yield ITEM_TYPES["score_choice"], f"{weight}|{label}|{note}"
        for top, code, label, meaning in card["bands"]:
            yield ITEM_TYPES["score_band"], f"{top}|{code}|{label}|{meaning}"
    calc = o.get("calculator")
    if calc:
        yield ITEM_TYPES["calc"], "|".join(
            [calc["algorithm"], str(calc["prior"]), str(calc["confirm"]), str(calc["reject"]),
             ",".join(str(c) for c in calc["choices"])])
    for axis in o.get("rubric", []):
        yield ITEM_TYPES["axis"], axis["name"]
        for code, label, meaning in axis["levels"]:
            yield ITEM_TYPES["level"], f"{code}|{label}|{meaning}"
    for ch in o["chapters"]:
        yield ITEM_TYPES["heading"], ch["heading"]
        for piece in chunks(ch["text"]):
            yield ITEM_TYPES["text"], piece


def encode(ir):
    validate(ir)
    objects = sorted(ir["objects"], key=lambda o: (o["category"], fold(o["title"]).lower(),
                                                   o["id"]))
    if len(objects) >= 0xFFFF:
        raise PocketError("pack exceeds 65534 objects")
    quiz = derive_quiz(objects)
    quizzed = {q["subject"] for q in quiz}
    index = {o["id"]: i for i, o in enumerate(objects)}
    s = Strings()
    pack = {**ir["pack"], "pack_id": ir["pack"]["id"]}
    meta = b"".join(STR.pack(*s.ref(str(pack.get(k, "")))) for k in META_FIELDS)
    kinds = b"".join(STR.pack(*s.ref(k)) for k in KINDS)
    relnames = b"".join(STR.pack(*s.ref(r)) for r in RELATIONS)
    cats, obj_bytes, items, rels = [], bytearray(), bytearray(), bytearray()
    for i, o in enumerate(objects):
        if not cats or cats[-1][0] != o["category"]:
            cats.append([o["category"], i, 0])
        cats[-1][2] += 1
        first_item = len(items) // ITEM.size
        for kind, text in object_items(o):
            items += ITEM.pack(kind, 0, *s.ref(text))
        n_items = len(items) // ITEM.size - first_item
        first_rel = len(rels) // RELATION.size
        for r in o["relations"]:
            rels += RELATION.pack(RELATIONS.index(r["type"]) + 1, 0, index[r["target"]])
        caps = sum(CAPS[c] for c in capabilities(o, i in quizzed))
        obj_bytes += OBJECT.pack(
            *s.ref(o["id"]), *s.ref(o.get("code", "")), *s.ref(o["title"]),
            *s.ref(o["definition"], MAX_DEFINITION), len(cats) - 1,
            KINDS.index(o["kind"]) + 1, caps, first_item, n_items, first_rel, len(o["relations"]))
    cat_bytes = b"".join(CATEGORY.pack(*s.ref(n), first, count) for n, first, count in cats)
    quiz_bytes = b"".join(QUIZ.pack(q["kind"], q["answer"], *q["options"], *s.ref(q["prompt"]),
                                    *s.ref(q["context"]), *s.ref(q["explanation"]),
                                    *s.ref(q["response"])) for q in quiz)
    body = [(SEC_META, meta, len(META_FIELDS)), (SEC_STRINGS, bytes(s.pool), len(s.pool)),
            (SEC_CATEGORIES, cat_bytes, len(cats)), (SEC_OBJECTS, bytes(obj_bytes), len(objects)),
            (SEC_ITEMS, bytes(items), len(items) // ITEM.size),
            (SEC_RELATIONS, bytes(rels), len(rels) // RELATION.size),
            (SEC_QUIZ, quiz_bytes, len(quiz)), (SEC_KINDS, kinds, len(KINDS)),
            (SEC_RELNAMES, relnames, len(RELATIONS))]
    offset = HEADER.size + SECTION.size * len(body)
    directory, payload, strings_at = bytearray(), bytearray(), 0
    for kind, data, count in body:
        if kind == SEC_STRINGS:
            strings_at = offset + len(payload)
        directory += SECTION.pack(kind, ENTRY_SIZE[kind], offset + len(payload), len(data), count)
        payload += data
        payload += b"\0" * (-len(payload) % 4)
    rest = bytes(directory) + bytes(payload)
    size = HEADER.size + len(rest)
    sb = strings_at - HEADER.size
    crc_index = zlib.crc32(rest[sb + len(s.pool):], zlib.crc32(rest[:sb]))
    header = HEADER.pack(MAGIC, FORMAT_VERSION, 0, size, crc_index, len(body), 0, len(objects),
                         zlib.crc32(bytes(s.pool)), 0)
    return header + rest


def decode(data, verify_strings=False):
    """Reference reader: the same checks the C reader makes, then a canonical dump."""
    if len(data) < HEADER.size:
        raise PocketError("truncated header")
    magic, ver, flags, size, crc, nsec, _r, nobj, crc_strings, _r2 = HEADER.unpack_from(data)
    if magic != MAGIC:
        raise PocketError("bad magic")
    if ver != FORMAT_VERSION:
        raise PocketError(f"unsupported format version {ver}")
    if flags:
        raise PocketError(f"unsupported required features {flags:#x}")
    if size != len(data):
        raise PocketError("file size mismatch")
    dir_end = HEADER.size + nsec * SECTION.size
    if nsec > 32 or dir_end > size:
        raise PocketError("section directory out of bounds")
    sec = {}
    for k in range(nsec):
        kind, esize, off, length, count = SECTION.unpack_from(data, HEADER.size + k * SECTION.size)
        if off + length > size or off < dir_end:
            raise PocketError(f"section {kind} out of bounds")
        if kind in ENTRY_SIZE:
            if esize != ENTRY_SIZE[kind] or length != esize * count:
                raise PocketError(f"section {kind} malformed")
            sec[kind] = (off, length, count)
    missing = [k for k in ENTRY_SIZE if k not in sec]
    if missing:
        raise PocketError(f"missing sections {missing}")
    spans = sorted((off, off + length) for off, length, _c in sec.values() if length)
    if any(spans[i][1] > spans[i + 1][0] for i in range(len(spans) - 1)):
        raise PocketError("sections overlap")
    soff, slen, _ = sec[SEC_STRINGS]
    if zlib.crc32(data[soff + slen:], zlib.crc32(data[HEADER.size:soff])) != crc:
        raise PocketError("checksum mismatch")
    if verify_strings and zlib.crc32(data[soff:soff + slen]) != crc_strings:
        raise PocketError("string pool checksum mismatch")

    def string(at):
        o, n = STR.unpack_from(data, at)
        if o + n > slen or n > MAX_STR:
            raise PocketError("string out of bounds")
        return data[soff + o: soff + o + n].decode("latin-1")

    def table(kind, st):
        off, _l, count = sec[kind]
        return [(off + i * st.size, st.unpack_from(data, off + i * st.size)) for i in range(count)]

    nobjects, ncats = sec[SEC_OBJECTS][2], sec[SEC_CATEGORIES][2]
    if nobjects != nobj or sec[SEC_META][2] < len(META_FIELDS):
        raise PocketError("object or meta count mismatch")
    kinds = [string(at) for at, _ in table(SEC_KINDS, STR)]
    relnames = [string(at) for at, _ in table(SEC_RELNAMES, STR)]
    items = [(row[0], string(at + 2)) for at, row in table(SEC_ITEMS, ITEM)]
    if any(not 1 <= t <= len(ITEM_TYPES) for t, _x in items):
        raise PocketError("item type out of range")
    rels = [row for _at, row in table(SEC_RELATIONS, RELATION)]
    if any(t[2] >= nobjects or not 1 <= t[0] <= len(relnames) for t in rels):
        raise PocketError("relation out of range")
    objects = []
    for at, row in table(SEC_OBJECTS, OBJECT):
        cat, kind, caps, fi, ni, fr, nr = row[8:15]
        if (fi + ni > len(items) or fr + nr > len(rels) or not 1 <= kind <= len(kinds)
                or cat >= ncats):
            raise PocketError("object references out of range")
        objects.append({
            "id": string(at), "code": string(at + 6), "title": string(at + 12),
            "definition": string(at + 18), "category": cat, "kind": kinds[kind - 1],
            "caps": sorted(c for c, bit in CAPS.items() if caps & bit),
            "items": [{"type": t, "text": x} for t, x in items[fi: fi + ni]],
            "relations": [{"type": relnames[t[0] - 1], "target": t[2]}
                          for t in rels[fr: fr + nr]],
        })
    cats = []
    for at, row in table(SEC_CATEGORIES, CATEGORY):
        if row[2] + row[3] > nobjects:
            raise PocketError("category out of range")
        cats.append({"name": string(at), "first": row[2], "count": row[3]})
    quiz = []
    for at, row in table(SEC_QUIZ, QUIZ):
        kind, answer, opts = row[0], row[1], list(row[2:6])
        if kind not in (QUIZ_DEFINITION, QUIZ_TRAINER) or answer > (3 if kind == 1 else 1) \
                or any(x >= nobjects for x in opts):
            raise PocketError("quiz out of range")
        quiz.append({"kind": kind, "answer": answer, "options": opts,
                     "prompt": string(at + 10), "context": string(at + 16),
                     "explanation": string(at + 22), "response": string(at + 28)})
    meta_off = sec[SEC_META][0]
    meta = {k: string(meta_off + i * STR.size) for i, k in enumerate(META_FIELDS)}
    return {"meta": meta, "categories": cats, "objects": objects, "quiz": quiz}


# ---------------------------------------------------------------- detection reference

ANSWERS = {"O": "observed", "N": "not_observed", "U": "unknown", "A": "not_applicable",
           "?": "unanswered"}


def summary(answers):
    """Counts per state. Never a score: absent evidence is not contrary evidence."""
    counts = {v: 0 for v in ANSWERS.values()}
    for a in answers:
        counts[ANSWERS[a]] += 1
    return counts


# ---------------------------------------------------------------- CLI


def dumps(obj):
    return json.dumps(obj, ensure_ascii=False, indent=1, sort_keys=True) + "\n"


def default_source():
    """KOMPENDIUM_DIR when set (CI checks the source out there), else the sibling checkout."""
    env = os.environ.get("KOMPENDIUM_DIR")
    return Path(env) if env else ROOT.parent / "kompendium-epistemicke-anihilace"


def pack_report(name, data):
    dump = decode(data, verify_strings=True)
    objs = dump["objects"]
    by_kind, by_type = {}, {}
    for o in objs:
        by_kind[o["kind"]] = by_kind.get(o["kind"], 0) + 1
        for it in o["items"]:
            by_type[it["type"]] = by_type.get(it["type"], 0) + 1
    names = {v: k for k, v in ITEM_TYPES.items()}
    return {"pack": name, "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest(),
            "objects": len(objs), "categories": len(dump["categories"]),
            "relations": sum(len(o["relations"]) for o in objs),
            "quiz": {"definition": sum(q["kind"] == 1 for q in dump["quiz"]),
                     "trainer": sum(q["kind"] == 2 for q in dump["quiz"])},
            "kinds": dict(sorted(by_kind.items())),
            "items": {names[k]: v for k, v in sorted(by_type.items())},
            "capabilities": {c: sum(c in o["caps"] for o in objs) for c in CAPS}}


def main(argv):
    if not argv:
        print(__doc__, file=sys.stderr)
        return 2
    cmd, args = argv[0], argv[1:]
    flags = {a for a in args if a == "--strings"}
    rest = [a for a in args if a not in flags]
    opts = dict(zip(rest[::2], rest[1::2])) if cmd not in ("decode", "summary") else {}
    spec = json.loads(SOURCES.read_text())
    pid = spec["pack"]["id"]
    ir_default = ROOT / "build" / "packs" / f"{pid}.ir.json"
    try:
        if cmd in ("extract", "build"):
            ir, report = extract(Path(opts.get("--source", default_source())))
            validate(ir)
            out = Path(opts.get("--out", ir_default))
            out.parent.mkdir(parents=True, exist_ok=True)
            out.write_text(dumps(ir))
            (out.parent / f"{pid}.extract.json").write_text(dumps(report))
            print(json.dumps({k: (len(v) if isinstance(v, list) else v)
                              for k, v in report.items()}, indent=1))
            if report["rejected"]:
                print("\n".join(report["rejected"][:20]), file=sys.stderr)
                return 1
            if cmd == "extract":
                return 0
            opts = {"--ir": str(out)}
            cmd = "compile"
        if cmd == "compile":
            ir = json.loads(Path(opts.get("--ir", ir_default)).read_text())
            data = encode(ir)
            out = Path(opts.get("--out", ROOT / "build" / "packs" / f"{pid}.pkp"))
            out.parent.mkdir(parents=True, exist_ok=True)
            out.write_bytes(data)
            report = pack_report(out.name, data)
            Path(opts.get("--report", out.with_suffix(".report.json"))).write_text(dumps(report))
            print(json.dumps(report, indent=1))
            return 0
        if cmd == "subset":
            ir = json.loads(Path(opts.get("--ir", ir_default)).read_text())
            keep = [o for o in ir["objects"] if o["id"].startswith(opts["--prefix"])]
            ids = {o["id"] for o in keep}
            for o in keep:
                o["relations"] = [r for r in o["relations"] if r["target"] in ids]
            Path(opts["--out"]).write_text(dumps({**ir, "objects": keep}))
            print(f"{len(keep)} objects")
            return 0
        if cmd == "decode":
            data = Path(rest[0]).read_bytes()
            sys.stdout.write(dumps(decode(data, verify_strings=bool(flags))))
            return 0
        if cmd == "summary":
            print(json.dumps(summary(rest[0]), sort_keys=True))
            return 0
    except PocketError as e:
        print(f"pocket: {e}", file=sys.stderr)
        return 1
    print(f"unknown command {cmd!r}", file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
