#!/usr/bin/env python3
# ----------------------------------------------------------------------------------------------
# Copyright (c) The Einsums Developers. All rights reserved.
# Licensed under the MIT License. See LICENSE.txt in the project root for license information.
# ----------------------------------------------------------------------------------------------
"""Shared helpers for the apiary docs-JSON schema (the cross-frontend contract).

One schema, one set of helpers. Both the merge stage (``apiary_merge_docs_json.py``)
and the renderer (``apiary_render_docs_rst.py``) import from here so the module-join
key and the entity-identity rules live in exactly one place. The schema itself
is documented in ``docs/docs_json_schema.md``; the field-by-field authority is
``src/DocsJson.cpp``.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

# Bump in lockstep with ``k_docs_json_schema_version`` in src/DocsJson.hpp.
SCHEMA_VERSION = 3

# The top-level documentable arrays, in document order. These are the entities
# that carry ``origin`` and participate in merge/de-dupe/collision detection.
TOP_LEVEL_KINDS = ("classes", "functions", "enums", "typedefs", "concepts", "macros")

DEFAULT_TOP = "einsums"


def log(prefix: str, msg: str) -> None:
    print(f"{prefix}: {msg}", file=sys.stderr)


def load_document(path: str, *, prefix: str = "apiary_docs") -> dict:
    """Load and lightly validate one docs-JSON document (fragment or merged)."""
    text = sys.stdin.read() if path == "-" else Path(path).read_text()
    doc = json.loads(text)
    version = doc.get("schema_version")
    if version != SCHEMA_VERSION:
        log(prefix, f"warning: {path} has schema_version {version}, expected {SCHEMA_VERSION}")
    return doc


def full_module(entity: dict, top: str) -> str:
    """Resolve the dotted Python module an entity belongs to (the join key).

    ``submodule`` is null for the top-level module, or a dotted path that may
    already include the top module (``einsums.linalg``) or be relative
    (``linalg``); normalize both forms. This is the single shared module-join
    key: both the C++ frontend (APIARY_MODULE-routed) and the Python frontend
    (file-path-derived) feed ``submodule``, and entities that normalize to the
    same dotted module belong to the same module group.
    """
    sub = entity.get("submodule")
    if not sub:
        return top
    if sub == top or sub.startswith(top + "."):
        return sub
    return f"{top}.{sub}"


def _signature(entity: dict) -> tuple:
    """A hashable signature of a callable's parameters, for de-dupe."""
    return tuple((p.get("name", ""), p.get("py_type", ""), p.get("type", "")) for p in entity.get("params", []))


def entity_dedupe_key(kind: str, entity: dict) -> tuple:
    """Cross-fragment de-dupe key.

    Same kind + same qualified name is "the same entity" — EXCEPT for functions,
    where overloads (C++ overload sets, or Python ``@overload`` defs) legitimately
    share a qualified name and must all survive. For those the parameter
    signature joins the key, so genuine cross-fragment duplicates (the same
    declaration visited in several TUs) still collapse while distinct overloads
    do not."""
    base = entity.get("qualified_name") or entity.get("name", "")
    if kind == "functions":
        return (kind, base, _signature(entity))
    return (kind, base)


def py_identity(entity: dict, top: str) -> tuple[str, str]:
    """Cross-origin collision key: the Python-visible identity of an entity —
    the dotted module it lands in plus the Python name it binds under."""
    return (full_module(entity, top), entity.get("py_name") or entity.get("name", ""))
