#!/usr/bin/env bash
# ----------------------------------------------------------------------------------------------
# Copyright (c) The Einsums Developers. All rights reserved.
# Licensed under the MIT License. See LICENSE.txt in the project root for license information.
# ----------------------------------------------------------------------------------------------
#
# apiary_doc_lint.py test. Runs the doc-quality validator over a docs-JSON fixture
# seeded with every drift class and diffs the diagnostics against a committed
# golden, then checks a clean input exits 0.
#
# The ``malformed-rest`` check needs docutils, which is not guaranteed here (the
# suite is otherwise pure Python and must run anywhere). It is exercised by its
# own seeded case below, kept out of the golden precisely so the golden means
# the same thing with or without docutils installed. When docutils is missing
# the case is skipped LOUDLY - a quietly-degraded markup check is the failure
# mode the check exists to fix.
#
# Updating the golden: run with REGEN=1.
#
# Invocation:
#     run_doc_lint.sh [python-executable]

set -euo pipefail

readonly PY="${1:-python3}"
readonly SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
readonly LINT="${ROOT}/scripts/apiary_doc_lint.py"
readonly FIXTURE="${SCRIPT_DIR}/fixtures/doc_lint_drift.json"
readonly GOLDEN="${SCRIPT_DIR}/golden/doc_lint_drift.txt.golden"
readonly REGEN="${REGEN:-0}"

fail=0

# The drift fixture carries no markup defects, so selecting the semantic checks
# explicitly produces byte-identical output to the default set - which keeps one
# golden valid on installs with and without docutils.
readonly SEMANTIC="unknown-param,unknown-tparam,missing-param,missing-tparam,returns-on-void,malformed-throws"

# --- drift fixture: diagnostics must match the golden, exit status must be 1 ---
out="$("${PY}" "${LINT}" --select "${SEMANTIC}" "${FIXTURE}" 2>/dev/null)" && rc=0 || rc=$?

if [[ "${REGEN}" == "1" ]]; then
    printf '%s\n' "${out}" > "${GOLDEN}"
    echo "regenerated ${GOLDEN}"
else
    if ! diff -u "${GOLDEN}" <(printf '%s\n' "${out}"); then
        echo "FAIL: doc_lint output does not match golden" >&2
        fail=1
    fi
    if [[ "${rc}" -ne 1 ]]; then
        echo "FAIL: expected exit 1 on drift, got ${rc}" >&2
        fail=1
    fi
fi

# --- clean input: no findings, exit 0 ---
clean="$(mktemp)"
trap 'rm -f "${clean}"' EXIT
cat > "${clean}" <<'JSON'
{ "schema_version": 1, "module": "clean",
  "classes": [], "functions": [], "enums": [], "typedefs": [], "concepts": [], "macros": [] }
JSON
if "${PY}" "${LINT}" --select "${SEMANTIC}" "${clean}" >/dev/null 2>&1; then
    : # exit 0 as expected
else
    echo "FAIL: clean input should exit 0" >&2
    fail=1
fi

# --- malformed-rest: a broken bullet list must be an error, at the header ---
if "${PY}" -c "import docutils" >/dev/null 2>&1; then
    broken="$(mktemp)"
    trap 'rm -f "${clean}" "${broken}"' EXIT
    # A bullet item whose wrapped continuation landed at column 0 - the exact
    # shape a Doxygen comment converts to when a continuation line is mishandled.
    cat > "${broken}" <<'JSON'
{ "schema_version": 1, "module": "broken",
  "classes": [], "enums": [], "typedefs": [], "concepts": [], "macros": [],
  "functions": [ {
    "name": "thing", "qualified_name": "demo::thing", "params": [],
    "location": { "file": "demo.hpp", "line": 7, "column": 5 },
    "doc_structured": {
      "brief": "Demo.",
      "detail": "- An item whose continuation line\nlanded at column zero.\n- A second item.",
      "params": [], "tparams": [], "returns": "", "throws": []
    } } ] }
JSON
    out="$("${PY}" "${LINT}" --select malformed-rest "${broken}" 2>/dev/null)" && rc=0 || rc=$?
    if [[ "${rc}" -ne 1 ]]; then
        echo "FAIL: malformed reST should exit 1, got ${rc}" >&2
        fail=1
    fi
    if [[ "${out}" != *"demo.hpp:7:5: error: demo::thing: malformed reST in detail"* ]]; then
        echo "FAIL: expected a located malformed-rest diagnostic, got: ${out}" >&2
        fail=1
    fi
    # ...and well-formed markup must stay silent.
    if ! "${PY}" "${LINT}" --select malformed-rest "${clean}" >/dev/null 2>&1; then
        echo "FAIL: clean input should exit 0 under malformed-rest" >&2
        fail=1
    fi
else
    echo "doc_lint: SKIPPED malformed-rest - docutils is not installed" >&2
fi

if [[ "${fail}" -eq 0 ]]; then
    echo "doc_lint: OK"
fi
exit "${fail}"
