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

# --- drift fixture: diagnostics must match the golden, exit status must be 1 ---
out="$("${PY}" "${LINT}" "${FIXTURE}" 2>/dev/null)" && rc=0 || rc=$?

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
if "${PY}" "${LINT}" "${clean}" >/dev/null 2>&1; then
    : # exit 0 as expected
else
    echo "FAIL: clean input should exit 0" >&2
    fail=1
fi

if [[ "${fail}" -eq 0 ]]; then
    echo "doc_lint: OK"
fi
exit "${fail}"
