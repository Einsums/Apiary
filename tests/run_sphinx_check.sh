#!/usr/bin/env bash
# ----------------------------------------------------------------------------------------------
# Copyright (c) The Einsums Developers. All rights reserved.
# Licensed under the MIT License. See LICENSE.txt in the project root for license information.
# ----------------------------------------------------------------------------------------------
#
# Sphinx build-check: the Python API reST the renderer emits must actually build.
# Runs the examples/mathx docs pipeline (C++ + Python -> merge -> render with
# content) and builds the result with ``sphinx-build -W -n`` — warnings are
# errors and missing cross-references are flagged — so a malformed directive,
# option block, or dangling ``:py:…:`` xref fails the build.
#
# Invocation:
#     run_sphinx_check.sh <apiary-binary> <apiary-include-dir> <sphinx-build> <python>

set -euo pipefail

if [[ $# -ne 4 ]]; then
    echo "usage: $0 <apiary-binary> <apiary-include-dir> <sphinx-build> <python>" >&2
    exit 64
fi

readonly TOOL="$1"
readonly INCLUDE_DIR="$2"
readonly SPHINX="$3"
readonly PY="$4"
readonly SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly REPO_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
readonly SCRIPTS_DIR="${REPO_DIR}/scripts"
readonly EX="${REPO_DIR}/examples/mathx"

WORK="$(mktemp -d)"
trap 'rm -rf "${WORK}"' EXIT
mkdir -p "${WORK}/src"

# Render the example's unified reference (C++ + Python + authored content).
"${TOOL}" --emit-docs-json --module mathx "${EX}/include/mathx/Vec.hpp" \
    -- -std=c++20 -nostdinc++ "-I${INCLUDE_DIR}" 2>/dev/null > "${WORK}/cpp.json"
"${PY}" "${SCRIPTS_DIR}/apiary_py_extract.py" --package mathx \
    --package-dir "${EX}/python/mathx" --source-root "${EX}" -o "${WORK}/py.json"
"${PY}" "${SCRIPTS_DIR}/apiary_merge_docs_json.py" -o "${WORK}/docs.json" \
    "${WORK}/cpp.json" "${WORK}/py.json" 2>/dev/null
"${PY}" "${SCRIPTS_DIR}/apiary_render_docs_rst.py" --outdir "${WORK}/src" \
    --content-dir "${EX}/content" "${WORK}/docs.json" 2>/dev/null

# A minimal Sphinx project; the pages only use the built-in py/std domains.
cat > "${WORK}/src/conf.py" <<'CONF'
project = "mathx"
extensions = []
html_theme = "alabaster"
root_doc = "index"
CONF

# -W: warnings are errors. -n: nitpicky (flag unresolved cross-references).
"${SPHINX}" -W -n -q -b html "${WORK}/src" "${WORK}/html"
[[ -f "${WORK}/html/index.html" ]] || { echo "FAIL: sphinx produced no index.html" >&2; exit 1; }

echo "PASS: run_sphinx_check"
