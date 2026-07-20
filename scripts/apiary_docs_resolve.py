#!/usr/bin/env python3
# ----------------------------------------------------------------------------------------------
# Copyright (c) The Einsums Developers. All rights reserved.
# Licensed under the MIT License. See LICENSE.txt in the project root for license information.
# ----------------------------------------------------------------------------------------------
"""Resolve docs-graph references against the merged docs-JSON.

Phase 2 of the DocC-like docs graph. Builds an index over the merged document
(every entity's Python-visible dotted path + short name) and resolves author
**symbol links** — written ``[[Type/member]]`` in docstrings — and type names to
reST py-domain cross-reference targets.

This is the single source of truth for "is this reference valid, and what does
it point to?", shared by the renderer (``apiary_render_docs_rst.py``, to emit
links) and the linter (``apiary_doc_lint.py``, to report unresolved references).

The link token is a symbol path; ``/``, ``.`` and ``::`` are accepted
separators and a trailing ``()`` is ignored:

    [[Decomposition]]            -> the class
    [[Decomposition/factor]]     -> a member, by partial path
    [[einsums.linalg.solve]]     -> a fully-qualified path

Resolution tries, in order: exact full dotted path, component-aligned suffix
match, then bare short name. A token that matches nothing — or more than one
distinct symbol — does not resolve (the latter is reported as ambiguous).

Run directly to report unresolved references in a merged document::

    apiary_docs_resolve.py --check docs.json
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import dataclass

from apiary_docs_schema import DEFAULT_TOP, full_module

# ``[[Type/member]]`` author link syntax; the inner token is a symbol path.
LINK_RE = re.compile(r"\[\[([^\]\n]+)\]\]")

# entity kind -> reST py-domain role.
_PY_ROLE = {
    "class": "class",
    "enum": "class",
    "function": "func",
    "method": "meth",
    "attribute": "attr",
    "data": "data",
}

# The doc-text fields of an entity that may carry ``[[ ]]`` links.
_TEXT_FIELDS = ("brief", "detail", "returns")


@dataclass(frozen=True)
class Entry:
    """A resolved reference target."""
    symbol_id: str
    target: str   # dotted py-domain target, e.g. einsums.linalg.Decomposition.factor
    role: str     # reST py role: class / func / meth / attr
    kind: str     # class / function / enum / method / attribute


def _normalize(token: str) -> str:
    t = (token or "").strip().replace("::", ".").replace("/", ".")
    t = re.sub(r"\(.*\)$", "", t).strip()  # drop a trailing call form
    return t.strip(".")


class Resolver:
    """Index over the merged docs graph, keyed by Python-visible dotted path."""

    def __init__(self, top: str = DEFAULT_TOP):
        self.top = top
        self.by_path: dict[str, Entry] = {}
        self.by_short: dict[str, list[Entry]] = {}
        self.by_symbol: dict[str, str] = {}  # symbol_id -> doc path

    # ── building ─────────────────────────────────────────────────────────────

    def _add(self, path: str, kind: str, symbol_id: str) -> str:
        entry = Entry(symbol_id, path, _PY_ROLE.get(kind, "obj"), kind)
        # First definition wins (stable); short-name index keeps every entry so
        # ambiguity can be detected.
        self.by_path.setdefault(path, entry)
        self.by_short.setdefault(path.rsplit(".", 1)[-1], []).append(entry)
        if symbol_id:
            self.by_symbol.setdefault(symbol_id, path)
        return path

    def path_for_symbol(self, symbol_id: str) -> str | None:
        """The doc path of an entity by its symbol_id (for type-curation lookup)."""
        return self.by_symbol.get(symbol_id)

    def class_paths(self) -> set[str]:
        """Doc paths of every class — the stems a type-curation file may target."""
        return {e.target for e in self.by_path.values() if e.kind == "class"}

    def _path(self, entity: dict, parent: str | None) -> str:
        pyname = entity.get("py_name") or entity.get("name", "")
        return f"{parent}.{pyname}" if parent else f"{full_module(entity, self.top)}.{pyname}"

    def _add_class(self, cls: dict, parent: str | None) -> None:
        path = self._add(self._path(cls, parent), "class", cls.get("symbol_id", ""))
        # Constructors are the Python ``__init__``, not a member named after the
        # class — indexing them under the class name would shadow the class
        # itself in short-name / suffix resolution.
        for m in cls.get("constructors", []):
            self._add(f"{path}.__init__", "method", m.get("symbol_id", ""))
        for m in cls.get("methods", []):
            self._add(self._path(m, path), "method", m.get("symbol_id", ""))
        for p in cls.get("properties", []):
            self._add(self._path(p, path), "attribute", p.get("symbol_id", ""))
        for fld in cls.get("fields", []):
            self._add(self._path(fld, path), "attribute", fld.get("symbol_id", ""))
        for en in cls.get("enums", []):
            self._add(self._path(en, path), "enum", en.get("symbol_id", ""))
        for nested in cls.get("nested_classes", []):
            self._add_class(nested, path)

    def add_document(self, doc: dict) -> None:
        for c in doc.get("classes", []):
            self._add_class(c, parent=None)
        for f in doc.get("functions", []):
            self._add(self._path(f, None), "function", f.get("symbol_id", ""))
        for e in doc.get("enums", []):
            self._add(self._path(e, None), "enum", e.get("symbol_id", ""))
        for v in doc.get("variables", []):
            self._add(self._path(v, None), "data", v.get("symbol_id", ""))

    # ── resolving ────────────────────────────────────────────────────────────

    def resolve(self, token: str) -> Entry | None:
        tok = _normalize(token)
        if not tok:
            return None
        if tok in self.by_path:
            return self.by_path[tok]
        # Component-aligned suffix match across full paths (de-duped by target).
        cands = {e.target: e for p, e in self.by_path.items() if p == tok or p.endswith("." + tok)}
        if len(cands) == 1:
            return next(iter(cands.values()))
        if len(cands) > 1:
            return None  # ambiguous
        # Bare short name.
        ids = {e.target: e for e in self.by_short.get(tok.rsplit(".", 1)[-1], [])}
        return next(iter(ids.values())) if len(ids) == 1 else None

    def class_and_enum_names(self) -> set[str]:
        """Short names of every class/enum — for the renderer's type sanitizer."""
        return {e.target.rsplit(".", 1)[-1] for e in self.by_path.values() if e.kind in ("class", "enum")}


def build_resolver(docs) -> Resolver:
    """Build a resolver from a merged doc (dict) or a list of docs."""
    if isinstance(docs, dict):
        docs = [docs]
    top = next((d.get("module") for d in docs if d.get("module")), DEFAULT_TOP)
    r = Resolver(top)
    for d in docs:
        r.add_document(d)
    return r


# ── link rewriting (renderer) ────────────────────────────────────────────────


def rest_role(entry: Entry) -> str:
    """reST cross-reference for a resolved entry; ``~`` shows only the leaf."""
    return f":py:{entry.role}:`~{entry.target}`"


def rewrite_links(text: str, resolver: Resolver) -> str:
    """Replace ``[[token]]`` with a reST role when resolved, else the leaf text."""
    def sub(m: re.Match) -> str:
        entry = resolver.resolve(m.group(1))
        if entry is not None:
            return rest_role(entry)
        return _normalize(m.group(1)).rsplit(".", 1)[-1] or m.group(1)

    return LINK_RE.sub(sub, text or "")


def find_links(text: str) -> list[str]:
    return [m.group(1) for m in LINK_RE.finditer(text or "")]


# ── diagnostics (linter) ─────────────────────────────────────────────────────


def _iter_entities(doc: dict):
    """Yield every entity that carries doc text + a location (incl. members)."""
    def walk_class(c):
        yield c
        for m in c.get("constructors", []) + c.get("methods", []) + c.get("properties", []) + c.get("fields", []):
            yield m
        for en in c.get("enums", []):
            yield en
        for n in c.get("nested_classes", []):
            yield from walk_class(n)

    for f in doc.get("functions", []):
        yield f
    for e in doc.get("enums", []):
        yield e
    for c in doc.get("classes", []):
        yield from walk_class(c)


def _entity_link_tokens(entity: dict):
    """Yield ``[[ ]]`` tokens found in an entity's doc text fields."""
    ds = entity.get("doc_structured") or {}
    texts = [ds.get(k) or "" for k in _TEXT_FIELDS]
    texts += [p.get("description") or "" for p in ds.get("params", [])]
    texts += [t.get("description") or "" for t in ds.get("throws", [])]
    texts.append(entity.get("doc") or "")
    for t in texts:
        yield from find_links(t)


def unresolved_references(doc: dict, resolver: Resolver) -> list[tuple[str, int, str]]:
    """Return (file, line, token) for every ``[[ ]]`` link that does not resolve.

    De-duplicates identical (location, token) so a link repeated in brief+detail
    is reported once."""
    out: list[tuple[str, int, str]] = []
    seen: set[tuple[str, int, str]] = set()
    for entity in _iter_entities(doc):
        loc = entity.get("location") or {}
        file, line = loc.get("file", "<unknown>"), int(loc.get("line", 0))
        for token in _entity_link_tokens(entity):
            if resolver.resolve(token) is not None:
                continue
            key = (file, line, token)
            if key in seen:
                continue
            seen.add(key)
            out.append(key)
    return out


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description="Resolve / check docs-graph references in a merged docs.json.")
    ap.add_argument("--check", required=True, help="merged docs.json to scan for unresolved [[ ]] references")
    args = ap.parse_args(argv)

    with open(args.check, encoding="utf-8") as fh:
        doc = json.load(fh)
    resolver = build_resolver(doc)
    findings = unresolved_references(doc, resolver)
    for file, line, token in sorted(findings):
        print(f"{file}:{line}: unresolved reference '[[{token}]]'")
    print(f"apiary_docs_resolve: {len(findings)} unresolved reference(s)", file=sys.stderr)
    return 1 if findings else 0


if __name__ == "__main__":
    raise SystemExit(main())
