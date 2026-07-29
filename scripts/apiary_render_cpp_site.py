#!/usr/bin/env python3
# ----------------------------------------------------------------------------------------------
# Copyright (c) The Einsums Developers. All rights reserved.
# Licensed under the MIT License. See LICENSE.txt in the project root for license information.
# ----------------------------------------------------------------------------------------------
"""Render a per-entity C++ API reference site for one module from apiary
``--emit-cpp-docs-json`` output.

Where ``apiary_render_cpp_rst.py`` renders ONE page per header (every entity of
the header on that page), this renderer consumes ALL of a module's per-header
JSON files and emits one page per entity - a page per class, per concept, and
per free-function overload set, plus catch-all pages for enums, type aliases,
operators, and macros - and a module index that leads with a link-per-symbol
summary (mirroring the Python renderer's module pages).

Rules that keep the Sphinx cpp domain sound:

* Entities are merged across headers by (kind, qualified name); a symbol is
  declared by a cpp directive on exactly ONE page. Every listing elsewhere is
  a ``:cpp:any:`` link, never a re-declaration, so the build needs no
  ``duplicate_declaration`` suppression.
* Page file names (slugs) are pure functions of the qualified name
  (``einsums::Tensor`` -> ``einsums.Tensor.rst``), so URLs move only when code
  renames. Slugs are checked for case-insensitive collisions (case-preserving
  file systems and static hosting collide on them); collisions get a
  deterministic kind suffix and any residual collision is a hard error.
* Labels contain neither slashes nor dots: ``api_cpp_einsums_Tensor``.

Usage::

    apiary_render_cpp_site.py --outdir <dir> --module-title Tensor \\
        --index-label modules_Einsums_Tensor_api \\
        [--backlink-label modules_Einsums_Tensor] [--label-prefix api_cpp] \\
        header1.json header2.json ...
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import apiary_render_cpp_rst as base  # noqa: E402  (signature lowering + directive rendering)
from apiary_io import write_if_changed  # noqa: E402

IND = base.IND
LICENSE_HEADER = base.LICENSE_HEADER

GENERATED_NOTE = [
    ".. note::",
    f"{IND}Generated from the C++ headers by ``apiary --emit-cpp-docs-json``.",
    "",
]


def log(msg: str) -> None:
    print(f"render_cpp_site: {msg}", file=sys.stderr)


# -- Entity model ------------------------------------------------------------


def has_doc(entity: dict) -> bool:
    ds = entity.get("doc_structured") or {}
    return bool((ds.get("brief") or "").strip() or (ds.get("detail") or "").strip())


def brief_of(entity: dict) -> str:
    """The one-line brief, whitespace-collapsed, for summary listings."""
    ds = entity.get("doc_structured") or {}
    return " ".join((ds.get("brief") or "").split())


def header_of(entity: dict) -> str | None:
    """The include-relative header path (after ``include/``) an entity was
    declared in, from its clang location."""
    loc = entity.get("location") or {}
    f = (loc.get("file") or "").replace("\\", "/")
    if "/include/" not in f:
        return None
    return f.split("/include/", 1)[1]


class Entity:
    """One merged entity: the best-documented occurrence plus every header
    that declares it."""

    def __init__(self, kind: str, data: dict):
        self.kind = kind  # class | concept | function | enum | typedef | macro
        self.data = data
        self.headers: set[str] = set()
        h = header_of(data)
        if h:
            self.headers.add(h)
        # Function overload sets collect every distinct-signature occurrence.
        self.overloads: list[dict] = [data] if kind == "function" else []

    @property
    def qualified_name(self) -> str:
        return self.data.get("qualified_name") or self.data["name"]

    @property
    def name(self) -> str:
        return self.data["name"]

    def merge(self, other: dict) -> None:
        h = header_of(other)
        if h:
            self.headers.add(h)
        if self.kind == "function":
            self.overloads.append(other)
        elif has_doc(other) and not has_doc(self.data):
            self.data = other


def is_identifier(name: str) -> bool:
    return re.fullmatch(r"[A-Za-z_]\w*", name) is not None


def collect(docs: list[dict]) -> dict[str, dict[str, Entity]]:
    """Merge all documents into per-kind maps keyed by qualified name."""
    kinds: dict[str, dict[str, Entity]] = {
        "class": {}, "concept": {}, "function": {}, "enum": {}, "typedef": {}, "macro": {},
    }

    def add(kind: str, e: dict) -> None:
        key = e.get("qualified_name") or e["name"]
        bucket = kinds[kind]
        if key in bucket:
            bucket[key].merge(e)
        else:
            bucket[key] = Entity(kind, e)

    for doc in docs:
        for cl in doc.get("classes", []):
            if not cl.get("is_external"):
                add("class", cl)
        for c in doc.get("concepts", []):
            add("concept", c)
        for fn in doc.get("functions", []):
            add("function", fn)
        for en in doc.get("enums", []):
            add("enum", en)
        for td in doc.get("typedefs", []):
            add("typedef", td)
        for m in doc.get("macros", []):
            add("macro", m)
    return kinds


# -- Slugs and labels --------------------------------------------------------

KIND_SUFFIX = {"class": "cls", "concept": "concept", "function": "fn", "enum": "enum", "typedef": "type"}


def slug_of(qualified_name: str) -> str:
    return qualified_name.replace("::", ".")


def label_of(prefix: str, slug: str) -> str:
    return prefix + "_" + re.sub(r"[^A-Za-z0-9_]", "_", slug)


def assign_slugs(paged: list[Entity]) -> dict[str, Entity]:
    """Map slug -> entity, resolving case-insensitive collisions with a
    deterministic kind suffix. A residual collision is a hard error: two pages
    whose names differ only by case silently overwrite each other on
    case-preserving file systems and static hosts."""
    ordered = sorted(paged, key=lambda e: (e.qualified_name, e.kind))
    counts: dict[str, int] = {}
    for e in ordered:
        low = slug_of(e.qualified_name).lower()
        counts[low] = counts.get(low, 0) + 1
    by_slug: dict[str, Entity] = {}
    taken: set[str] = set()
    for e in ordered:
        s = slug_of(e.qualified_name)
        if counts[s.lower()] > 1:
            s = f"{s}-{KIND_SUFFIX[e.kind]}"
        if s.lower() in taken:
            raise SystemExit(
                f"render_cpp_site: slug collision even after kind suffix: {s} "
                f"({e.kind} {e.qualified_name}) - rename one of the entities' pages")
        by_slug[s] = e
        taken.add(s.lower())
    return by_slug


# -- Page rendering ----------------------------------------------------------


def page_head(title: str, label: str) -> list[str]:
    bar = "=" * len(title)
    return [LICENSE_HEADER, "", f".. _{label}:", "", bar, title, bar, "", *GENERATED_NOTE]


def defined_in(headers: set[str]) -> list[str]:
    if not headers:
        return []
    names = ", ".join(f"``{h}``" for h in sorted(headers))
    label = "headers" if len(headers) > 1 else "header"
    return [f"Defined in {label} {names}.", ""]


def namespace_block(ns: str) -> list[str]:
    return [f".. cpp:namespace:: {ns or '0'}", ""]


def doc_richness(entity: dict) -> tuple[int, int]:
    """Orderable doc quality: documented params/returns first, prose length
    second. A definition with @param docs beats its forward declaration."""
    ds = entity.get("doc_structured") or {}
    fields = len(ds.get("params") or []) + len(ds.get("tparams") or []) + (1 if ds.get("returns") else 0)
    prose = len((ds.get("brief") or "")) + len((ds.get("detail") or ""))
    return (fields, prose)


def dedup_signatures(fns: list[dict]) -> list[dict]:
    """Distinct-signature overloads, in a stable order. A forward declaration
    and its definition (or the same header parsed for two modules) produce
    identical signatures; keep the best-documented one."""
    by_sig: dict[str, dict] = {}
    order: list[str] = []
    for fn in fns:
        sig = base.function_signature(fn)
        if sig is None:
            continue
        if sig not in by_sig:
            by_sig[sig] = fn
            order.append(sig)
        elif doc_richness(fn) > doc_richness(by_sig[sig]):
            by_sig[sig] = fn
    return [by_sig[s] for s in order]


def render_entity_page(e: Entity, label: str) -> str | None:
    title = e.qualified_name
    out = page_head(title, label)
    out += defined_in(e.headers)
    ns = base.namespace_of(e.qualified_name)
    out += namespace_block(ns)
    if e.kind == "class":
        base.render_class(out, e.data)
    elif e.kind == "concept":
        base.render_concept(out, e.data)
    elif e.kind == "function":
        overloads = dedup_signatures(e.overloads)
        if not overloads:
            return None
        for fn in overloads:
            base.render_function(out, fn)
    else:
        raise ValueError(e.kind)
    return "\n".join(out).rstrip() + "\n"


def render_group_page(title: str, label: str, entities: list[Entity], renderer) -> str:
    """A catch-all page (enums / types / operators / macros): entities grouped
    by namespace, each declared here and nowhere else."""
    out = page_head(title, label)
    headers = {h for e in entities for h in e.headers}
    out += defined_in(headers)
    if renderer is base.render_macro:  # macros are C-domain, not namespaced
        for e in sorted(entities, key=lambda x: x.name):
            renderer(out, e.data)
    else:
        by_ns: dict[str, list[Entity]] = {}
        for e in entities:
            by_ns.setdefault(base.namespace_of(e.qualified_name), []).append(e)
        for ns in sorted(by_ns):
            out += namespace_block(ns)
            for e in sorted(by_ns[ns], key=lambda x: x.name):
                renderer(out, e.data)
    return "\n".join(out).rstrip() + "\n"


def render_operator_page(label: str, sets: list[Entity]) -> str | None:
    out = page_head("Operators", label)
    headers = {h for e in sets for h in e.headers}
    out += defined_in(headers)
    wrote = False
    by_ns: dict[str, list[Entity]] = {}
    for e in sets:
        by_ns.setdefault(base.namespace_of(e.qualified_name), []).append(e)
    for ns in sorted(by_ns):
        block: list[str] = []
        for e in sorted(by_ns[ns], key=lambda x: x.name):
            for fn in dedup_signatures(e.overloads):
                base.render_function(block, fn)
        if block:
            out += namespace_block(ns)
            out += block
            wrote = True
    if not wrote:
        return None
    return "\n".join(out).rstrip() + "\n"


# -- Module index ------------------------------------------------------------


def summary_item(role: str, target: str, brief: str) -> str:
    line = f"- :{role}:`{target}`"
    if brief:
        line += f" - {brief}"
    return line


def render_index(module_title: str, index_label: str, backlink_label: str,
                 sections: list[tuple[str, list[tuple[str, str, str]]]],
                 toctree: list[str]) -> str:
    title = f"{module_title} C++ API"
    bar = "=" * len(title)
    out = [LICENSE_HEADER, "", f".. _{index_label}:", "", bar, title, bar, "", *GENERATED_NOTE]
    if backlink_label:
        # Explicit link text: a bare :ref: fails when the label does not
        # directly precede a section title.
        out += [f"See the :ref:`narrative documentation <{backlink_label}>` of this module for "
                "background and usage guidance.", ""]
    if not any(items for _, items in sections):
        out += ["No public API is documented in this module yet.", ""]
    for heading, items in sections:
        if not items:
            continue
        out += [heading, "-" * max(len(heading), 3), ""]
        for role, target, brief in items:
            out.append(summary_item(role, target, brief))
        out.append("")
    if toctree:
        out += [".. toctree::", f"{IND}:maxdepth: 1", f"{IND}:hidden:", ""]
        for entry in toctree:
            out.append(f"{IND}{entry}")
        out.append("")
    return "\n".join(out).rstrip() + "\n"


# -- Site assembly -----------------------------------------------------------


def render_site(docs: list[dict], outdir: Path, module_title: str, index_label: str,
                backlink_label: str, label_prefix: str) -> list[Path]:
    kinds = collect(docs)

    # Entities that get their own page.
    paged: list[Entity] = list(kinds["class"].values()) + list(kinds["concept"].values())
    operator_sets: list[Entity] = []
    for e in kinds["function"].values():
        if is_identifier(e.name):
            paged.append(e)
        else:
            operator_sets.append(e)
    by_slug = assign_slugs(paged)

    # The renderer owns this directory: a renamed or deleted entity must not
    # leave its old page behind. Pruning happens AFTER the writes, against the
    # set actually produced, rather than by clearing the directory up front --
    # clearing first would make every write_if_changed() see a missing file and
    # rewrite it, which resets the mtime of every page and costs Sphinx a full
    # re-read of the reference on a build where nothing changed.
    outdir.mkdir(parents=True, exist_ok=True)
    written: list[Path] = []

    def write(name: str, text: str) -> None:
        p = outdir / name
        write_if_changed(p, text)
        written.append(p)

    sections: dict[str, list[tuple[str, str, str]]] = {
        "Classes": [], "Concepts": [], "Functions": [], "Operators": [],
        "Enumerations": [], "Types": [], "Macros": [],
    }
    toctree: list[str] = []

    section_of = {"class": "Classes", "concept": "Concepts", "function": "Functions"}
    for slug in sorted(by_slug, key=str.lower):
        e = by_slug[slug]
        text = render_entity_page(e, label_of(label_prefix, slug))
        if text is None:
            continue
        write(slug + ".rst", text)
        toctree.append(slug)
        sections[section_of[e.kind]].append(("cpp:any", f"~{e.qualified_name}", brief_of(e.data)))

    # Catch-all pages exist once per module, so their labels are scoped by
    # the module's (globally unique) index label, not the shared prefix.
    if operator_sets:
        text = render_operator_page(f"{index_label}_operators", operator_sets)
        if text is not None:
            write("operators.rst", text)
            toctree.append("operators")
            for e in sorted(operator_sets, key=lambda x: x.name):
                sections["Operators"].append(("cpp:any", f"~{e.qualified_name}", brief_of(e.data)))

    group_specs = [
        ("enum", "Enumerations", "enums", base.render_enum),
        ("typedef", "Types", "types", base.render_typedef),
        ("macro", "Macros", "macros", base.render_macro),
    ]
    for kind, heading, stem, renderer in group_specs:
        entities = list(kinds[kind].values())
        if not entities:
            continue
        write(stem + ".rst", render_group_page(heading, f"{index_label}_{stem}", entities, renderer))
        toctree.append(stem)
        for e in sorted(entities, key=lambda x: x.qualified_name):
            if kind == "macro":
                sections[heading].append(("c:macro", e.name, brief_of(e.data)))
            else:
                sections[heading].append(("cpp:any", f"~{e.qualified_name}", brief_of(e.data)))

    ordered_sections = [(h, sections[h]) for h in
                        ("Classes", "Concepts", "Functions", "Operators", "Enumerations", "Types", "Macros")]
    write("index.rst", render_index(module_title, index_label, backlink_label, ordered_sections, toctree))

    # Prune pages this run did not produce: an entity that was renamed or
    # removed would otherwise keep a page that still builds and still resolves
    # references to something that no longer exists.
    keep = {p.name for p in written}
    for old in outdir.glob("*.rst"):
        if old.name not in keep:
            old.unlink()

    return written


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("inputs", nargs="*", help="the module's per-header cpp-docs JSON files")
    ap.add_argument("--outdir", required=True, help="directory for the module's pages")
    ap.add_argument("--module-title", required=True, help="module display name (e.g. Tensor)")
    ap.add_argument("--index-label", required=True,
                    help="Sphinx label for the module index page (preserves inbound :ref:s)")
    ap.add_argument("--backlink-label", default="",
                    help="label of the module's narrative page; empty for no backlink")
    ap.add_argument("--label-prefix", default="api_cpp", help="prefix for per-entity page labels")
    args = ap.parse_args()

    docs = []
    for p in args.inputs:
        text = Path(p).read_text(encoding="utf-8")
        if text.strip():
            docs.append(json.loads(text))
    written = render_site(docs, Path(args.outdir), args.module_title,
                          args.index_label, args.backlink_label, args.label_prefix)
    log(f"wrote {len(written)} pages into {args.outdir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
