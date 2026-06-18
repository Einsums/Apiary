# mathx — a complete Apiary example (C++ + Python, one merged reference)

`greeter` shows the C++ binding path. **`mathx` shows the whole documentation
pipeline**: a compiled C++ core *and* a hand-written Python layer, extracted by
two frontends and merged into one cross-linked reference with authored curation
and articles.

```
include/mathx/Vec.hpp     C++ core: Vec2 (class) + dot (free function)   ─┐ Clang frontend
src/module.cpp            PYBIND11_MODULE(_core) body                     │
python/mathx/__init__.py  package shell: re-exports + version() + __all__ ─┐ static `ast`
python/mathx/extras.py    convenience helpers: normalize / lerp / unit    ─┘ frontend
content/*.md              authored overview, curation (## Topics), article
```

## What it demonstrates

- **Two frontends, one graph.** The C++ `Vec2`/`dot` and the Python
  `version`/`normalize`/`lerp` land on the same `mathx` and `mathx.extras`
  pages, keyed by the shared dotted-module join.
- **Cross-language links.** `mathx.extras.normalize` is documented with
  ``[[Vec2/length]]`` and a `Vec2` annotation; both resolve to the
  **C++-bound** `Vec2` on its own page.
- **Aliases, not duplicates.** `__init__` re-exports `Vec2`/`dot`/`normalize`
  via `__all__`; they are documented once, at their definition site.
- **Curation + articles.** `content/mathx.md` and `content/mathx.extras.md`
  give each page an overview and `## Topics` groups; `content/mathx.Vec2.md`
  curates `Vec2`'s own members under `.. rubric::` headings; and
  `content/getting-started.md` is a free-standing guide.
- **Availability.** `Vec2` carries `@since 1.0.0`; `mathx.extras.unit` is
  `@deprecated` with a `.. deprecated:: 1.2.0` version.
- **Navigation.** Each page leads with a link-rich Summary; the index leads
  with a Modules overview.

## Build & run

Apiary must be discoverable by `find_package(Apiary)` (install it and point
`CMAKE_PREFIX_PATH` at the prefix, or replace the `find_package` call with
`add_subdirectory(<path-to-apiary> apiary)`).

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=/tmp/apiary
cmake --build build                          # extension + staged Python package
PYTHONPATH=build python3 test_mathx.py        # -> "mathx example: OK"
```

## Build the docs

The docs target is on demand (not part of `all`):

```bash
cmake --build build --target mathx_docs
ls build/docs/        # index.rst  mathx.rst  mathx.extras.rst  getting-started.rst
```

That target runs the full pipeline wired in `CMakeLists.txt`:

```
apiary_add_bindings(... DOCS_JSON ...)      # C++  -> mathx_core.docs.json
apiary_add_python_docs(...)                 # Python -> mathx.py.docs.json
apiary_aggregate_extension(
    DOCS_JSON <cpp>  PY_DOCS_JSON <py>       # both fragments ...
    DOCS_CONTENT_DIR content                 # ... + authored content ...
    DOCS_TARGET mathx_docs)                  # -> merge -> link-check -> render
```

The generated `.rst` is Sphinx-ready (it uses the `py` domain); drop it into a
Sphinx project's `toctree` to get HTML. To make unresolved `[[ ]]` links fail
the build, add `--strict` to the `apiary_doc_lint.py --check-links` step.

## The pipeline, by hand

The CMake target is just these steps; you can run them directly against an
in-tree Apiary build:

```bash
APIARY=/path/to/apiary-build           # contains the `apiary` binary + scripts/
S=$APIARY/../scripts                    # or the installed share/apiary/scripts

$APIARY/apiary --emit-docs-json --module mathx include/mathx/Vec.hpp \
    -- -std=c++20 -I/path/to/apiary/include > cpp.docs.json
python3 $S/apiary_py_extract.py --package mathx --package-dir python/mathx \
    --source-root . -o py.docs.json
python3 $S/apiary_merge_docs_json.py -o docs.json cpp.docs.json py.docs.json
python3 $S/apiary_render_docs_rst.py --outdir docs --content-dir content docs.json
```
