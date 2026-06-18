#!/usr/bin/env bash
# ----------------------------------------------------------------------------------------------
# Copyright (c) The Einsums Developers. All rights reserved.
# Licensed under the MIT License. See LICENSE.txt in the project root for license information.
# ----------------------------------------------------------------------------------------------
#
# Static-Python-frontend suite. Drives the pure-Python docs pipeline:
#
#     apiary_py_extract.py  ->  apiary_merge_docs_json.py  ->  apiary_render_docs_rst.py
#
# Verification follows the agreed policy: PRIMARY assertions are on the
# structured docs.json (origin, dotted module, signature extraction, collision
# resolution); plus ONE loose end-to-end .rst presence check (no pinned
# golden — proves the renderer handles the Python-origin shape without
# inheriting .rst-golden brittleness).
#
# Pure Python — needs no apiary binary (a committed C++-origin fragment fixture
# stands in for the C++ frontend), so it runs in any environment.
#
# Invocation:
#     run_py_extract.sh <python-executable>

set -euo pipefail

if [[ $# -ne 1 ]]; then
    echo "usage: $0 <python-executable>" >&2
    exit 64
fi

readonly PY="$1"
readonly SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly REPO_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
readonly SCRIPTS_DIR="${REPO_DIR}/scripts"
readonly FIXTURE_DIR="${SCRIPT_DIR}/fixtures"
readonly PKG_DIR="${FIXTURE_DIR}/pypkg/einsums"
readonly CPP_FRAG="${FIXTURE_DIR}/linalg_cpp_fragment.docs.json"

WORK="$(mktemp -d)"
trap 'rm -rf "${WORK}"' EXIT

readonly PY_FRAG="${WORK}/einsums.py.docs.json"
readonly MERGED="${WORK}/docs.json"
readonly MERGED2="${WORK}/docs2.json"
readonly RST_DIR="${WORK}/rst"

fail() { echo "FAIL: $*" >&2; exit 1; }

# jq-free JSON probing: a tiny python helper that prints a python expression
# evaluated against the loaded document `d`.
jget() { "${PY}" -c 'import json,sys; d=json.load(open(sys.argv[1])); print(eval(sys.argv[2]))' "$1" "$2"; }

assert_eq()       { [[ "$2" == "$3" ]] || fail "$1: expected '$3', got '$2'"; }
assert_contains() { grep -qE -- "$2" "$1" || { echo "--- $1 ---" >&2; cat "$1" >&2; fail "missing pattern in $1: $2"; }; }
assert_absent()   { ! grep -qE -- "$2" "$1" || fail "forbidden pattern in $1: $2"; }

# ── 1. Extract ───────────────────────────────────────────────────────────────
# --source-root makes location.file repo-relative (for clean source links).
"${PY}" "${SCRIPTS_DIR}/apiary_py_extract.py" --package einsums --package-dir "${PKG_DIR}" \
    --source-root "${REPO_DIR}" -o "${PY_FRAG}"

# Helper to find an entity by name across a doc's functions/classes.
find_expr() {
    # $1 kind, $2 name -> python expr selecting the matching entity dict
    echo "[e for e in d['$1'] if e['name']=='$2'][0]"
}

# Schema + origin
assert_eq "schema_version" "$(jget "${PY_FRAG}" "d['schema_version']")" "4"
assert_eq "top module"     "$(jget "${PY_FRAG}" "d['module']")"          "einsums"

solve="$(find_expr functions solve)"
assert_eq "solve.origin"       "$(jget "${PY_FRAG}" "${solve}['origin']")"        "python"
assert_eq "solve.submodule"    "$(jget "${PY_FRAG}" "${solve}['submodule']")"     "einsums.linalg"
assert_eq "solve.qualified"    "$(jget "${PY_FRAG}" "${solve}['qualified_name']")" "einsums.linalg.solve"
# Keyword-only arg with default captured correctly (defaults align to the tail).
assert_eq "solve.assume_a default" "$(jget "${PY_FRAG}" "[p for p in ${solve}['params'] if p['name']=='assume_a'][0]['default']")" "'gen'"
assert_eq "solve param names" "$(jget "${PY_FRAG}" "[p['name'] for p in ${solve}['params']]")" "['a', 'b', 'assume_a']"
# Binding-centric fields present but empty (no consumer should assume otherwise).
assert_eq "solve.is_template" "$(jget "${PY_FRAG}" "${solve}['is_template']")" "False"
assert_eq "solve.instantiations" "$(jget "${PY_FRAG}" "${solve}['instantiations']")" "[]"

# __init__ free function lands on the TOP module (submodule == top).
version="$(find_expr functions version)"
assert_eq "version.submodule" "$(jget "${PY_FRAG}" "${version}['submodule']")" "einsums"

# Private helper must NOT be in the documented surface.
assert_eq "private excluded" "$(jget "${PY_FRAG}" "'_backend_solve' in [f['name'] for f in d['functions']]")" "False"

# Class + method: self dropped, vararg kept, kwonly default captured.
decomp="$(find_expr classes Decomposition)"
assert_eq "Decomposition.origin" "$(jget "${PY_FRAG}" "${decomp}['origin']")" "python"
assert_eq "factor params" "$(jget "${PY_FRAG}" "[p['name'] for p in [m for m in ${decomp}['methods'] if m['name']=='factor'][0]['params']]")" "['matrix', '*factors', 'pivoting']"

echo "ok: fragment extraction"

# ── 1b. Annotation recovery (py_type from ast.unparse) ───────────────────────
assert_eq "solve.assume_a py_type" "$(jget "${PY_FRAG}" "[p for p in ${solve}['params'] if p['name']=='assume_a'][0]['py_type']")" "str"
assert_eq "solve.a py_type empty"  "$(jget "${PY_FRAG}" "[p for p in ${solve}['params'] if p['name']=='a'][0]['py_type']")" ""

cho="$(find_expr functions cho_factor)"
assert_eq "cho_factor return py_type" "$(jget "${PY_FRAG}" "${cho}['return_py_type']")" "Decomposition"
assert_eq "cho_factor matrix py_type" "$(jget "${PY_FRAG}" "${cho}['params'][0]['py_type']")" "ArrayLike"

factor="[m for m in ${decomp}['methods'] if m['name']=='factor'][0]"
assert_eq "factor.pivoting py_type" "$(jget "${PY_FRAG}" "[p for p in ${factor}['params'] if p['name']=='pivoting'][0]['py_type']")" "bool"
assert_eq "factor return py_type"   "$(jget "${PY_FRAG}" "${factor}['return_py_type']")" "Decomposition"

echo "ok: annotation recovery"

# ── 1c. Decorators / properties / overloads / numpydoc ───────────────────────
# __init__ routed to constructors, not methods.
assert_eq "ctors"        "$(jget "${PY_FRAG}" "[m['name'] for m in ${decomp}['constructors']]")" "['__init__']"
assert_eq "no __init__ in methods" "$(jget "${PY_FRAG}" "'__init__' in [m['name'] for m in ${decomp}['methods']]")" "False"
# Private method excluded.
assert_eq "private method excluded" "$(jget "${PY_FRAG}" "'_private_helper' in [m['name'] for m in ${decomp}['methods']]")" "False"
# @property + @rank.setter -> writable property.
assert_eq "rank property py_type" "$(jget "${PY_FRAG}" "[p for p in ${decomp}['properties'] if p['py_name']=='rank'][0]['py_type']")" "int"
assert_eq "rank property writable" "$(jget "${PY_FRAG}" "[p for p in ${decomp}['properties'] if p['py_name']=='rank'][0]['writable']")" "True"
# @staticmethod keeps first arg + sets is_static; @classmethod drops cls.
ident="[m for m in ${decomp}['methods'] if m['name']=='identity'][0]"
assert_eq "identity is_static"  "$(jget "${PY_FRAG}" "${ident}['is_static']")" "True"
assert_eq "identity params"     "$(jget "${PY_FRAG}" "[p['name'] for p in ${ident}['params']]")" "['n']"
fromk="[m for m in ${decomp}['methods'] if m['name']=='from_kind'][0]"
assert_eq "from_kind drops cls" "$(jget "${PY_FRAG}" "[p['name'] for p in ${fromk}['params']]")" "['kind']"
# @overload -> one entity per signature, sharing py_name; impl docstring borrowed.
assert_eq "norm overload count" "$(jget "${PY_FRAG}" "len([f for f in d['functions'] if f['py_name']=='norm'])")" "2"
assert_eq "norm sigs distinct"  "$(jget "${PY_FRAG}" "len({tuple(p['name'] for p in f['params']) for f in d['functions'] if f['py_name']=='norm'})")" "2"
assert_eq "norm doc borrowed"   "$(jget "${PY_FRAG}" "[f for f in d['functions'] if f['py_name']=='norm'][0]['doc_structured']['brief']")" "Compute a vector or matrix norm."
# numpydoc: Parameters / Returns parsed into doc_structured.
assert_eq "solve numpydoc params" "$(jget "${PY_FRAG}" "[p['name'] for p in ${solve}['doc_structured']['params']]")" "['a', 'b', 'assume_a']"
assert_eq "solve numpydoc returns" "$(jget "${PY_FRAG}" "bool(${solve}['doc_structured']['returns'])")" "True"

echo "ok: decorators / properties / overloads / numpydoc"

# ── 1d. __all__ + re-export aliases ──────────────────────────────────────────
# version is local AND in __all__ -> documented on the top module.
assert_eq "version present (top)" "$(jget "${PY_FRAG}" "'version' in [f['name'] for f in d['functions'] if f['submodule']=='einsums']")" "True"
# debug_dump is local but NOT in __all__ -> excluded (private by declaration).
assert_eq "debug_dump excluded" "$(jget "${PY_FRAG}" "'debug_dump' in [f['name'] for f in d['functions']]")" "False"
# solve/Decomposition are re-exports (aliases): documented at their definition
# site only, NOT re-minted as einsums.solve / einsums.Decomposition.
assert_eq "no top-level solve alias" "$(jget "${PY_FRAG}" "len([f for f in d['functions'] if f['name']=='solve' and f['submodule']=='einsums'])")" "0"
assert_eq "no top-level Decomposition alias" "$(jget "${PY_FRAG}" "len([c for c in d['classes'] if c['name']=='Decomposition' and c['submodule']=='einsums'])")" "0"
assert_eq "solve only at definition site" "$(jget "${PY_FRAG}" "[f['submodule'] for f in d['functions'] if f['name']=='solve']")" "['einsums.linalg']"
echo "ok: __all__ + re-export aliases"

# ── 1e. Symbol IDs + relationship edges (docs graph) ─────────────────────────
assert_eq "solve symbol_id"          "$(jget "${PY_FRAG}" "${solve}['symbol_id']")" "py:einsums.linalg.solve"
assert_eq "Decomposition symbol_id"  "$(jget "${PY_FRAG}" "${decomp}['symbol_id']")" "py:einsums.linalg.Decomposition"
assert_eq "factor symbol_id"         "$(jget "${PY_FRAG}" "[m for m in ${decomp}['methods'] if m['name']=='factor'][0]['symbol_id']")" "py:einsums.linalg.Decomposition.factor"
# memberOf: factor -> Decomposition.
assert_eq "memberOf edge" "$(jget "${PY_FRAG}" "any(e['kind']=='memberOf' and e['source']=='py:einsums.linalg.Decomposition.factor' and e['target']=='py:einsums.linalg.Decomposition' for e in d['edges'])")" "True"
# inheritsFrom: LUDecomposition -> Decomposition (base emitted as written name).
assert_eq "inheritsFrom edge" "$(jget "${PY_FRAG}" "any(e['kind']=='inheritsFrom' and e['source']=='py:einsums.linalg.LUDecomposition' and e['target']=='Decomposition' for e in d['edges'])")" "True"
echo "ok: symbol IDs + relationship edges"

# ── 2. Merge (cross-origin co-location + collision resolution) ───────────────
"${PY}" "${SCRIPTS_DIR}/apiary_merge_docs_json.py" -o "${MERGED}" "${CPP_FRAG}" "${PY_FRAG}" 2> "${WORK}/merge.err"

# Co-residence: C++-origin `lu` and Python-origin `Decomposition` both survive.
assert_eq "lu present (cpp)"  "$(jget "${MERGED}" "'lu' in [f['name'] for f in d['functions']]")" "True"
assert_eq "Decomposition present (python)" "$(jget "${MERGED}" "'Decomposition' in [c['name'] for c in d['classes']]")" "True"

# Collision: solve is documented by BOTH frontends in einsums.linalg. Resolve to
# cpp (single source of truth), emit exactly one `solve`, and warn.
assert_eq "solve count after merge" "$(jget "${MERGED}" "len([f for f in d['functions'] if f['py_name']=='solve'])")" "1"
assert_eq "solve resolved to cpp"   "$(jget "${MERGED}" "[f for f in d['functions'] if f['py_name']=='solve'][0]['origin']")" "cpp"
assert_contains "${WORK}/merge.err" "collision.*solve"

# Edges survive the merge (source entity survived).
assert_eq "inheritsFrom edge in merged" "$(jget "${MERGED}" "any(e['kind']=='inheritsFrom' and e['source']=='py:einsums.linalg.LUDecomposition' for e in d['edges'])")" "True"

echo "ok: merge + cross-origin collision resolution"

# ── 3. Idempotency ───────────────────────────────────────────────────────────
"${PY}" "${SCRIPTS_DIR}/apiary_merge_docs_json.py" -o "${MERGED2}" "${MERGED}" 2>/dev/null
diff "${MERGED}" "${MERGED2}" >/dev/null || fail "merge is not idempotent"
echo "ok: merge idempotent"

# ── 4. One loose end-to-end .rst presence check ──────────────────────────────
"${PY}" "${SCRIPTS_DIR}/apiary_render_docs_rst.py" --outdir "${RST_DIR}" "${MERGED}" 2>/dev/null
[[ -f "${RST_DIR}/einsums.linalg.rst" ]] || fail "no einsums.linalg.rst produced"
assert_contains "${RST_DIR}/einsums.linalg.rst" "Decomposition"
assert_contains "${RST_DIR}/einsums.linalg.rst" "factor"
assert_contains "${RST_DIR}/einsums.linalg.rst" "solve"
assert_contains "${RST_DIR}/einsums.linalg.rst" "lu"
assert_contains "${RST_DIR}/einsums.rst" "version"
# Cross-ref: a return annotation naming a documented class resolves through the
# renderer's KNOWN_TYPES path (rendered as the type, not collapsed to Any).
assert_contains "${RST_DIR}/einsums.linalg.rst" "-> Decomposition"
# An annotation naming an undocumented type collapses to Any for display.
assert_absent   "${RST_DIR}/einsums.linalg.rst" "ArrayLike"
# Phase-4 shapes render: a staticmethod directive, a property attribute, an
# overload set (two signatures), and numpydoc :param: field lists.
assert_contains "${RST_DIR}/einsums.linalg.rst" "py:staticmethod:: identity"
assert_contains "${RST_DIR}/einsums.linalg.rst" "py:attribute:: rank"
assert_contains "${RST_DIR}/einsums.linalg.rst" ":param ord:"
assert_contains "${RST_DIR}/einsums.linalg.rst" "ord: int"
# Phase-2 symbol links: a resolved [[Decomposition/factor]] becomes a py-domain
# cross-ref; the dangling [[NoSuchSymbol]] renders as plain text (brackets gone).
assert_contains "${RST_DIR}/einsums.linalg.rst" "py:meth:.~einsums.linalg.Decomposition.factor."
assert_contains "${RST_DIR}/einsums.linalg.rst" "NoSuchSymbol"
assert_absent   "${RST_DIR}/einsums.linalg.rst" "\[\["
echo "ok: end-to-end render presence + cross-ref + decorator shapes + symbol links"

# ── 5. Source links (.rst layer, origin-aware .py vs .hpp) ───────────────────
RST_LINKS="${WORK}/rst_links"
"${PY}" "${SCRIPTS_DIR}/apiary_render_docs_rst.py" --outdir "${RST_LINKS}" \
    --py-source-url-template 'https://example.org/consumer/blob/main/{file}#L{line}' \
    --cpp-source-url-template 'https://example.org/einsums/blob/main/{file}#L{line}' \
    "${MERGED}" 2>/dev/null
# Python-origin entity -> consumer repo, .py path.
assert_contains "${RST_LINKS}/einsums.linalg.rst" "example.org/consumer/blob/main/tests/fixtures/pypkg/einsums/linalg.py#L"
# C++-origin entity -> einsums repo, .hpp path.
assert_contains "${RST_LINKS}/einsums.linalg.rst" "example.org/einsums/blob/main/.*\.hpp#L"
# Without templates, no source links are emitted (back-compat default).
assert_absent   "${RST_DIR}/einsums.linalg.rst" "\[source\]"
echo "ok: origin-aware source links"

# ── 6. Reference-resolution diagnostics ──────────────────────────────────────
# Over the merged graph: the dangling [[NoSuchSymbol]] is reported; the valid
# [[Decomposition/factor]] is NOT. --select isolates the link check.
"${PY}" "${SCRIPTS_DIR}/apiary_doc_lint.py" --check-links --select unresolved-reference "${MERGED}" \
    > "${WORK}/links.out" 2>/dev/null || true
assert_contains "${WORK}/links.out" "unresolved reference '\[\[NoSuchSymbol\]\]'"
assert_absent   "${WORK}/links.out" "factor"
echo "ok: unresolved-reference diagnostics"

# ── 7. Curation + articles (Phase 3) ─────────────────────────────────────────
RST_CUR="${WORK}/rst_curated"
"${PY}" "${SCRIPTS_DIR}/apiary_render_docs_rst.py" --outdir "${RST_CUR}" \
    --content-dir "${FIXTURE_DIR}/pypkg_content" "${MERGED}" 2> "${WORK}/coverage.err"
# Module overview prose, with its [[ ]] links resolved to py-domain xrefs.
assert_contains "${RST_CUR}/einsums.linalg.rst" "linear-algebra convenience layer"
assert_contains "${RST_CUR}/einsums.linalg.rst" ":py:class:.~einsums.linalg.Decomposition."
# Curated topic-group headings, in authored order.
assert_contains "${RST_CUR}/einsums.linalg.rst" "^Decompositions$"
assert_contains "${RST_CUR}/einsums.linalg.rst" "^Solvers$"
# Uncurated symbols still render, under a by-kind fallback section.
assert_contains "${RST_CUR}/einsums.linalg.rst" "^Functions$"
assert_contains "${RST_CUR}/einsums.linalg.rst" "py:function:: norm"
# Free-standing article: its own page, an MD heading converted to reST, and a
# resolved [[ ]] link; listed in the index toctree under a Guides caption.
assert_contains "${RST_CUR}/getting-started.rst" "Getting Started"
assert_contains "${RST_CUR}/getting-started.rst" "^Installation$"
assert_contains "${RST_CUR}/getting-started.rst" ":py:func:.~einsums.linalg.solve."
assert_contains "${RST_CUR}/index.rst" ":caption: Guides"
assert_contains "${RST_CUR}/index.rst" "getting-started"
# Coverage diagnostic: an uncurated documented symbol is reported.
assert_contains "${WORK}/coverage.err" "'norm' is documented but not curated"
echo "ok: curation + articles + coverage"

echo "PASS: run_py_extract"
