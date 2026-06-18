<!--
----------------------------------------------------------------------------------------------
 Copyright (c) The Einsums Developers. All rights reserved.
 Licensed under the MIT License. See LICENSE.txt in the project root for license information.
----------------------------------------------------------------------------------------------
-->

# Apiary docs-JSON schema (the cross-frontend contract)

This is the authoritative description of the JSON document that every Apiary
extraction frontend emits and that the merge stage + renderer consume. It is
the **one contract** shared by:

- the **C++ frontend** (`apiary --emit-docs-json` / `--emit-cpp-docs-json`,
  implemented in `src/DocsJson.cpp`),
- the **static Python frontend** (`scripts/apiary_py_extract.py`, stdlib `ast` only),
- the **merge stage** (`scripts/apiary_merge_docs_json.py`), and
- the **renderer** (`scripts/apiary_render_docs_rst.py`).

There is exactly one schema. Per-frontend *fragments* and the merged
*document* have identical shape; merging is **idempotent** (merging an
already-merged document is a no-op). `schema_version` is currently **5**.

> The canonical field-by-field source of truth is still `src/DocsJson.cpp`
> (`emit_docs_json`). This document explains the shape, the join keys, and the
> `origin` semantics that the second frontend depends on; keep them in sync.

## Top-level document

```jsonc
{
  "schema_version": 5,
  "module": "einsums",          // top-level Python import name (NOT a submodule)
  "classes":   [ <class>...   ],
  "functions": [ <function>... ],
  "enums":     [ <enum>...     ],
  "typedefs":  [ <typedef>...  ],   // C++-origin, docs mode only
  "concepts":  [ <concept>...  ],   // C++-origin, docs mode only
  "macros":    [ <macro>...    ],   // C++-origin, docs mode only
  "variables": [ <variable>... ],   // module-level data / py:data (v5)
  "edges":     [ <edge>...     ]    // relationship graph (v4)
}
```

**variable** (`py:data`): a documentable entity plus `py_type` and `value`
(the constant's source, when simple). The C++ frontend emits none; the static
Python frontend fills them from module-level constants (skipping `__all__`).
The Python frontend also now extracts **nested classes** (`nested_classes`),
**`enum.Enum` subclasses** (as `enums` with `enumerators`), and **class-level
annotated attributes** (as `fields`) — previously silently dropped. Data
entities (variables, fields, enum members) take their `doc` from a **`#:` or
plain-`#` comment** — the contiguous block immediately above the assignment
(with a leading `# Section`-style header stripped) or a trailing comment on the
line — since they carry no docstring.

When several fragments are merged, the result is a single document of the same
shape: the arrays are concatenated and de-duplicated (see *Merge & collisions*).
All fragments in one merge must agree on `module`; the merge takes the first.

## `origin` (added in v3)

Every **top-level documentable entity** (`classes`, `functions`, `enums`,
`typedefs`, `concepts`, `macros`) carries:

```jsonc
"origin": "cpp" | "python"
```

- The C++ frontend always writes `"cpp"`.
- The Python frontend always writes `"python"`.

`origin` is per-entity (not per-document) so it survives merging a mixed set of
fragments. Nested members (methods/fields/properties/enumerators) do **not**
carry their own `origin`; they inherit their enclosing entity's. Source-link
generation does not need `origin` — it keys off `location.file`'s extension
(`.py` vs `.hpp`).

`origin` drives real behavior:

1. **Docstring grammar** is selected *at the producing frontend*: the C++
   frontend parses Doxygen into `doc_structured`; the Python frontend parses
   numpydoc into the same `doc_structured` shape. Downstream consumers are
   grammar-agnostic.
2. **Collision resolution** in the merge stage (below).
3. **Source links** in the renderer (`.py` vs `.hpp` repo paths).

## Join keys

- **Module routing:** the dotted Python module an entity belongs to is derived
  from `submodule` (an entity field), normalized against the top-level
  `module`. `null` → the top module; `"linalg"` → `"einsums.linalg"`;
  `"einsums.linalg"` → itself. The Python frontend sets `submodule` to the
  fully-qualified dotted module computed from the `.py` file path relative to
  the package root (`einsums/linalg.py` → `"einsums.linalg"`,
  `einsums/__init__.py` → `"einsums"`). This is the **shared module join key**:
  Python-origin entities land in the same module group as
  `APIARY_MODULE`-routed C++ entities.
- **Cross-TU / cross-fragment de-dupe** is by `qualified_name` (within a kind).
- **Cross-origin collision** is by `(dotted_module, py_name)` — C++ and Python
  `qualified_name`s never collide, but they can bind the same Python name.

## Entity shapes (abridged)

Common to every documentable entity:

```jsonc
{
  "name": "...",                 // unqualified source name
  "origin": "cpp" | "python",
  "symbol_id": "...",            // stable graph ID — see "Symbol IDs & edges"
  "py_name": "...",              // resolved Python identifier (naming authority is the producer)
  "hidden": false,               // bound but intentionally omitted from the Python surface
  "qualified_name": "...",       // ::ns::Class::m (cpp) or dotted path (python)
  "doc": "...",                  // raw docstring/comment body
  "doc_structured": {            // reST-ready, grammar already applied
    "brief": "...", "detail": "...",
    "params":  [ {"name","description"}... ],
    "tparams": [ {"name","description"}... ],
    "returns": "...",
    "throws":  [ {"name","description"}... ]
  },
  "location": { "file": "...", "line": 0, "column": 0 },
  "submodule": "einsums.linalg" | null,
  "directives": [ {"name","args":[...]}... ],  // [] for Python origin
  "availability": {                            // v4 — see "Availability"
    "since": "1.2.0" | null,
    "deprecated": false,
    "deprecated_since": "1.5.0" | null,
    "deprecated_note": "..." | null
  }
}
```

### Availability

`availability` is filled from `@since` / `@deprecated` (C++ Doxygen) and from a
`@deprecated` decorator + `.. versionadded::` / `.. deprecated::` directives
(Python). Both frontends produce the same shape, so the renderer emits identical
badges: a `.. versionadded::` for `since`; and for `deprecated`, the proper
`.. deprecated:: <version>` directive when `deprecated_since` is known, else a
*Deprecated* admonition — both carrying the migration note. `deprecated_since`
comes from Python's `.. deprecated:: <ver>`; Doxygen's `@deprecated` has no
standard version, so the C++ frontend leaves it null. The producing frontend
strips the lifted directive from the prose so it is not rendered twice.

- **function**: adds `return_type`, `return_type_canonical`, `return_py_type`,
  `params[]`, `is_template`, `template_params[]`, `template_kwargs[]`,
  `instantiations[]`, `python_overloads[]`. A Python-origin function fills
  `return_py_type` + `params[]` and leaves the binding-mechanics fields empty
  (`is_template:false`, `[]`).
- **param**: `{name, type, type_canonical, py_type, default, default_py}`. The
  renderer reads `py_type` and `default_py`/`default`. Python origin fills
  `py_type` (from `ast.unparse`) and `default`; leaves C++ `type` fields empty.
- **class**: adds `is_template`, `is_external`, `template_params[]`, `bases[]`,
  `instantiations[]`, `constructors[]`, `methods[]`, `properties[]`,
  `fields[]`, `enums[]`, `nested_classes[]`. Python origin fills
  `methods`/`properties`/`bases` and leaves `instantiations`/`is_template`
  empty.
- **enum**: adds `is_scoped`, `underlying_type`, `underlying_py_type`,
  `enumerators[] {name,value,doc,doc_structured}`.
- **property**: `{py_name, type, py_type, doc, doc_structured, writable}`.
- **python_overloads[]**: `{kind, py_name, instantiation_indices[],
  dtype_values[], default_dtype, kwarg_names[]}` — see `IR.hpp::PythonOverload`.
  Python origin uses `kind:"overload_set"` for `@overload` groups.

## Symbol IDs & edges (v4 — the relationship graph)

Every top-level documentable entity and every class member carries a stable,
language-tagged **`symbol_id`**:

- **C++:** `c++:<USR>` — the Clang USR from `clang::index::generateUSRForDecl`
  (the same identifiers DocC consumes). Precise and unique per declaration
  (overloads get distinct USRs).
- **Python:** `py:<dotted-name>` (e.g. `py:einsums.linalg.Decomposition.factor`).
  Python `@overload` variants share one `symbol_id` — it is the *logical*
  symbol a `Type/member` link resolves to.

`symbol_id` is empty only for entities with no underlying declaration (e.g.
synthesized protocol dunders). It is the **primary identity** for the graph and
(Phase 2) reference resolution — distinct from `qualified_name` (human-readable)
and the Python-visible `(submodule, py_name)` collision key.

The top-level **`edges`** array is the relationship graph; each edge is:

```jsonc
{ "source": "<symbol_id>", "target": "<symbol_id | name>", "kind": "..." }
```

| `kind` | meaning | source / target |
|---|---|---|
| `memberOf` | a class member belongs to its type | member → enclosing class |
| `inheritsFrom` | public base class | derived class → base |
| `overrides` | virtual override (C++ only) | method → overridden method |

`target` may be an unresolved/external reference (a base whose type has no
resolvable record, or a Python base emitted as its written name) — it is kept
verbatim for Phase 2's resolver. Top-level functions/enums carry their module
membership via `submodule`, so they get no `memberOf` edge. The Python frontend
emits `memberOf` + `inheritsFrom`; `overrides` is C++-only (it can't be
determined statically without MRO).

## Reference resolution (Phase 2)

Authors write **symbol links** in docstrings (C++ `///` or Python) as
`[[Type/member]]`. The inner token is a symbol path; `/`, `.` and `::` are
accepted separators and a trailing `()` is ignored:

```
[[Decomposition]]            the class
[[Decomposition/factor]]     a member, by partial path
[[einsums.linalg.solve]]     a fully-qualified path
```

`scripts/apiary_docs_resolve.py` indexes the merged graph by each entity's
Python-visible dotted path (`full_module` + the `py_name` chain) and resolves a
token by, in order: exact path, component-aligned suffix, then bare short name
(a token matching nothing — or more than one distinct symbol — does not
resolve). It is the single source of truth shared by:

- the **renderer**, which rewrites a resolved `[[ ]]` into a reST py-domain
  cross-reference (`:py:meth:`~einsums.linalg.Decomposition.factor``) and leaves
  an unresolved one as plain leaf text; and
- the **linter** (`apiary_doc_lint.py --check-links`, run over the *merged*
  docs.json so the resolver sees the whole graph), which reports every
  unresolved link as `file:line: unresolved reference '[[token]]'`.

## Authored content & curation (Phase 3)

A documentation set is more than a symbol reference. An optional
``--content-dir`` of authored Markdown (parsed by ``scripts/apiary_curation.py``)
layers two things on top of the generated pages:

- **Module curation** — a file whose stem is a documented dotted module
  (``einsums.linalg.md``). Prose before its ``## Topics`` section becomes the
  module page's overview; each ``### Group`` under ``## Topics`` is a curated
  group whose ``[[ ]]`` links name the symbols to render there, in order.
  Symbols no topic lists still render, auto-grouped by kind, so nothing
  silently disappears.
- **Type curation** — a file whose stem is a documented class path
  (``einsums.linalg.Decomposition.md``). Its prose is prepended to the class
  body, and each ``### Group`` under ``## Topics`` renders the named members
  under a ``.. rubric::`` heading inside the class; uncurated members follow.
- **Articles** — any other ``.md`` file: a free-standing page (overview, guide)
  added to the index toctree under a *Guides* caption.

Both may use the `[[ ]]` symbol links above. The renderer (`--content-dir`)
warns about curation gaps — a documented symbol no topic lists, or a curated
link that resolves to nothing. Curation is authored content, not part of the
docs-JSON schema; it consumes the same graph the JSON describes.

**Navigation.** Each module page leads with a **Summary** — a link-rich list of
its symbols (py-domain cross-reference + brief). The index page leads with a
**Modules** overview — a `:doc:` link per module plus a one-line summary (the
authored overview's first sentence, else symbol counts) — above the navigation
toctrees. Both are derived from the graph, so they stay in sync automatically.

## Merge & collisions

The merge stage (`scripts/apiary_merge_docs_json.py`):

1. Loads all fragments, validates `schema_version`.
2. Concatenates each kind, dropping `hidden` and `is_external` entities and
   de-duplicating by `(kind, qualified_name)`.
3. Detects collisions on `(dotted_module, py_name)` across origins. Resolution
   is deterministic and emits a drift warning to stderr so a single source of
   truth is rendered rather than a drifted pair.
4. Concatenates and de-dupes `edges`, dropping any whose `source` entity did not
   survive (so a collision-dropped class takes its members' edges with it).
5. Emits one canonical document of this same schema.

Re-running the merge over its own output is a no-op (idempotent).
