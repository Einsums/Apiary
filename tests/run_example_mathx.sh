#!/usr/bin/env bash
# ----------------------------------------------------------------------------------------------
# Copyright (c) The Einsums Developers. All rights reserved.
# Licensed under the MIT License. See LICENSE.txt in the project root for license information.
# ----------------------------------------------------------------------------------------------
#
# Drives the examples/mathx docs pipeline end to end — C++ frontend + Python
# frontend -> merge -> render with authored content — and asserts the key
# cross-language / curation / availability outputs. Guards the example and
# regression-tests resolver behavior on C++ classes (constructors must not
# shadow the class in [[ ]] resolution).
#
# Invocation:
#     run_example_mathx.sh <apiary-binary> <apiary-include-dir> <python>

set -euo pipefail

if [[ $# -ne 3 ]]; then
    echo "usage: $0 <apiary-binary> <apiary-include-dir> <python>" >&2
    exit 64
fi

readonly TOOL="$1"
readonly INCLUDE_DIR="$2"
readonly PY="$3"
readonly SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly REPO_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
readonly SCRIPTS_DIR="${REPO_DIR}/scripts"
readonly EX="${REPO_DIR}/examples/mathx"

WORK="$(mktemp -d)"
trap 'rm -rf "${WORK}"' EXIT

fail() { echo "FAIL: $*" >&2; exit 1; }
assert_contains() { grep -qE -- "$2" "$1" || { echo "--- $1 ---" >&2; cat "$1" >&2; fail "missing in $1: $2"; }; }
assert_absent()   { ! grep -qE -- "$2" "$1" || fail "forbidden in $1: $2"; }

# ── Pipeline ─────────────────────────────────────────────────────────────────
"${TOOL}" --emit-docs-json --module mathx "${EX}/include/mathx/Vec.hpp" \
    -- -std=c++20 -nostdinc++ "-I${INCLUDE_DIR}" 2>/dev/null > "${WORK}/cpp.json"
"${PY}" "${SCRIPTS_DIR}/apiary_py_extract.py" --package mathx \
    --package-dir "${EX}/python/mathx" --source-root "${EX}" -o "${WORK}/py.json"
"${PY}" "${SCRIPTS_DIR}/apiary_merge_docs_json.py" -o "${WORK}/docs.json" \
    "${WORK}/cpp.json" "${WORK}/py.json" 2>/dev/null
"${PY}" "${SCRIPTS_DIR}/apiary_render_docs_rst.py" --outdir "${WORK}/rst" \
    --content-dir "${EX}/content" "${WORK}/docs.json" 2> "${WORK}/render.err"

RST="${WORK}/rst"
[[ -f "${RST}/mathx.rst" && -f "${RST}/mathx.extras.rst" && -f "${RST}/getting-started.rst" ]] \
    || fail "expected pages not generated"

# Cross-language link: a Python doc resolves [[Vec2/length]] to the C++ method.
assert_contains "${RST}/mathx.extras.rst" ":py:meth:.~mathx.Vec2.length."
# Co-location: C++ (Vec2, dot) and Python (version) on the same top page.
assert_contains "${RST}/mathx.rst" "py:class:: Vec2"
assert_contains "${RST}/mathx.rst" "py:function:: dot"
assert_contains "${RST}/mathx.rst" "py:function:: version"
# Per-type curation on a C++ class: members under .. rubric:: headings.
assert_contains "${RST}/mathx.rst" "rubric:: Magnitude"
assert_contains "${RST}/mathx.rst" "rubric:: Transform"
# Availability: @since on the class, versioned deprecation on the Python alias.
assert_contains "${RST}/mathx.rst" "versionadded:: 1.0.0"
assert_contains "${RST}/mathx.extras.rst" "\.\. deprecated:: 1.2.0"
# Navigation summary + module overview.
assert_contains "${RST}/mathx.rst" "^Summary$"
assert_contains "${RST}/index.rst" "^Modules$"

# Regression: a bare [[Vec2]] (a C++ class with constructors) must resolve — the
# constructor must not shadow the class — so no such coverage warning appears.
assert_absent "${WORK}/render.err" "curated link \[\[Vec2\]\] resolves to nothing"

echo "PASS: run_example_mathx"
