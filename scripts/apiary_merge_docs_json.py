#!/usr/bin/env python3
# ----------------------------------------------------------------------------------------------
# Copyright (c) The Einsums Developers. All rights reserved.
# Licensed under the MIT License. See LICENSE.txt in the project root for license information.
# ----------------------------------------------------------------------------------------------
"""Merge per-frontend apiary docs-JSON fragments into one canonical document.

This is the standalone merge stage of the docs pipeline::

    frontends  ->  *.docs.json fragments  ->  apiary_merge_docs_json.py  ->  apiary_render_docs_rst.py
    (C++ + Python)                            (one canonical docs.json)

Each extraction frontend (the C++ tool's ``--emit-docs-json`` and the static
Python ``apiary_py_extract.py``) emits a fragment in the shared schema
(``docs/docs_json_schema.md``). This stage concatenates them, drops entities
that are not part of the documentable surface (``hidden`` / ``is_external``),
de-duplicates entities that surface in more than one fragment, detects
cross-origin collisions (the same Python identity documented by both
frontends) and resolves them deterministically, and writes ONE document in
the SAME schema. The renderer then consumes that single file.

The merge is idempotent: running it over its own output is a no-op.

Usage::

    apiary_merge_docs_json.py --output docs.json frag1.docs.json frag2.docs.json ...
    apiary --emit-docs-json ... | apiary_merge_docs_json.py --output docs.json -
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

from apiary_docs_schema import (
    DEFAULT_TOP,
    SCHEMA_VERSION,
    TOP_LEVEL_KINDS,
    entity_dedupe_key,
    full_module,
    iter_symbol_ids,
    load_document,
    py_identity,
)

PREFIX = "merge_docs_json"


def log(msg: str) -> None:
    print(f"{PREFIX}: {msg}", file=sys.stderr)


def is_dropped(entity: dict) -> bool:
    """Entities captured only for name resolution or deliberately hidden are
    not part of the documentable surface, so they never reach the renderer."""
    return bool(entity.get("hidden") or entity.get("is_external"))


# Deterministic cross-origin resolution. When the same Python identity
# (dotted_module, py_name) is documented by both frontends, the C++ binding is
# the authoritative runtime surface — a pure-Python symbol that shadows a bound
# name is almost always a wrapper/re-export that drifted (genuine re-exports
# are folded as aliases upstream, not emitted as standalone python symbols).
# Lower number wins.
_ORIGIN_PRIORITY = {"cpp": 0, "python": 1}


def merge(docs: list[dict]) -> dict:
    top = DEFAULT_TOP
    for doc in docs:
        top = doc.get("module", top)
        break

    out: dict = {"schema_version": SCHEMA_VERSION, "module": top}

    for kind in TOP_LEVEL_KINDS:
        # 1. Concatenate + drop non-surface + de-dupe by qualified name.
        seen: set[tuple[str, str]] = set()
        kept: list[dict] = []
        for doc in docs:
            for ent in doc.get(kind, []):
                if is_dropped(ent):
                    continue
                key = entity_dedupe_key(kind, ent)
                if key in seen:
                    continue
                seen.add(key)
                kept.append(ent)

        # 2. Cross-origin collision: the SAME Python identity (dotted module +
        #    py_name) documented by BOTH frontends. That is drift — resolve
        #    deterministically (C++ is the authoritative runtime surface) and
        #    warn. Same-origin entries sharing a py_name are NOT a collision:
        #    they are an overload set (C++ overloads, or Python @overload), so
        #    every member is preserved.
        groups: dict[tuple[str, str], list[dict]] = {}
        order: list[tuple[str, str]] = []
        for ent in kept:
            ident = py_identity(ent, top)
            if ident not in groups:
                groups[ident] = []
                order.append(ident)
            groups[ident].append(ent)

        resolved: list[dict] = []
        for ident in order:
            members = groups[ident]
            origins = {m.get("origin", "cpp") for m in members}
            if len(origins) > 1:
                # Keep only the winning origin's members (preserves an overload
                # set that happens to be entirely in the winning origin).
                winning = min(origins, key=lambda o: _ORIGIN_PRIORITY.get(o, 99))
                kept_members = [m for m in members if m.get("origin", "cpp") == winning]
                dropped = [m.get("origin", "cpp") for m in members if m.get("origin", "cpp") != winning]
                mod, name = ident
                log(
                    f"collision: {kind[:-1]} '{name}' in module '{mod}' documented by "
                    f"{sorted(origins)} frontends; keeping '{winning}' (single source of "
                    f"truth), dropped {len(dropped)} {sorted(set(dropped))} entr"
                    f"{'y' if len(dropped) == 1 else 'ies'}."
                )
                resolved.extend(kept_members)
            else:
                resolved.extend(members)

        out[kind] = resolved

    # 3. Relationship edges. An edge survives only if its SOURCE entity survived
    #    (collision resolution may have dropped a class and with it its members);
    #    targets may be unresolved/external and are kept verbatim for Phase 2's
    #    resolver. De-dupe identical edges. Order-stable -> idempotent.
    surviving: set[str] = set()
    for kind in TOP_LEVEL_KINDS:
        for ent in out[kind]:
            surviving.update(iter_symbol_ids(ent))

    seen_edges: set[tuple[str, str, str]] = set()
    edges: list[dict] = []
    for doc in docs:
        for e in doc.get("edges", []):
            if e.get("source") not in surviving:
                continue
            key = (e.get("source", ""), e.get("target", ""), e.get("kind", ""))
            if key in seen_edges:
                continue
            seen_edges.add(key)
            edges.append(e)
    out["edges"] = edges

    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("inputs", nargs="+", help="docs-JSON fragments (or '-' for stdin)")
    ap.add_argument("--output", "-o", required=True, help="path to write the merged docs.json (or '-' for stdout)")
    args = ap.parse_args()

    docs = [load_document(p, prefix=PREFIX) for p in args.inputs]
    merged = merge(docs)

    text = json.dumps(merged, indent=2) + "\n"
    if args.output == "-":
        sys.stdout.write(text)
    else:
        Path(args.output).write_text(text)
        counts = ", ".join(f"{len(merged.get(k, []))} {k}" for k in TOP_LEVEL_KINDS if merged.get(k))
        log(f"wrote {args.output} (module '{merged['module']}'; {counts or 'empty'})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
