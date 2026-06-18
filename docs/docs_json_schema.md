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
already-merged document is a no-op). `schema_version` is currently **3**.

> The canonical field-by-field source of truth is still `src/DocsJson.cpp`
> (`emit_docs_json`). This document explains the shape, the join keys, and the
> `origin` semantics that the second frontend depends on; keep them in sync.

## Top-level document

```jsonc
{
  "schema_version": 3,
  "module": "einsums",          // top-level Python import name (NOT a submodule)
  "classes":   [ <class>...   ],
  "functions": [ <function>... ],
  "enums":     [ <enum>...     ],
  "typedefs":  [ <typedef>...  ],   // C++-origin, docs mode only
  "concepts":  [ <concept>...  ],   // C++-origin, docs mode only
  "macros":    [ <macro>...    ]    // C++-origin, docs mode only
}
```

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
  "directives": [ {"name","args":[...]}... ]   // [] for Python origin
}
```

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

## Merge & collisions

The merge stage (`scripts/apiary_merge_docs_json.py`):

1. Loads all fragments, validates `schema_version`.
2. Concatenates each kind, dropping `hidden` and `is_external` entities and
   de-duplicating by `(kind, qualified_name)`.
3. Detects collisions on `(dotted_module, py_name)` across origins. Resolution
   is deterministic and emits a drift warning to stderr so a single source of
   truth is rendered rather than a drifted pair.
4. Emits one canonical document of this same schema.

Re-running the merge over its own output is a no-op (idempotent).
