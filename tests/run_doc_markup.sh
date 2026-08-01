#!/usr/bin/env bash
# ----------------------------------------------------------------------------------------------
# Copyright (c) The Einsums Developers. All rights reserved.
# Licensed under the MIT License. See LICENSE.txt in the project root for license information.
# ----------------------------------------------------------------------------------------------
#
# Doc-markup suite. Drives tests/fixtures/rest_shapes.hpp - one entity per reST
# shape the Doxygen converter has to get right - through apiary in docs-JSON
# mode, then pins TWO goldens:
#
#   1. rest_shapes.doc.txt.golden   the converted doc text, field by field.
#   2. rest_shapes.lint.txt.golden  the malformed-rest diagnostics over it.
#
# Both are needed, and they catch different halves of the problem. A conversion
# can be structurally valid reST and still be wrong - an inline command that
# lost its markup, a nested list flattened to one level - and only the text
# golden sees that. A conversion can also be textually plausible and not parse
# at all, which is what reaches Sphinx as a warning about a generated file, and
# only the lint golden sees that.
#
# The lint golden is NOT expected to be empty. It records current behavior,
# including the shapes rest_shapes.hpp marks KNOWN BAD; when one of those is
# fixed, its diagnostic leaves the golden and the diff is the proof.
#
# Updating goldens: run with REGEN=1.
#
# Invocation:
#     run_doc_markup.sh <apiary-binary> <annotations-include-dir> <python>

set -euo pipefail

if [[ $# -ne 3 ]]; then
    echo "usage: $0 <apiary-binary> <annotations-include-dir> <python>" >&2
    exit 64
fi

readonly TOOL="$1"
readonly INCLUDE_DIR="$2"
readonly PY="$3"
readonly SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
readonly SCRIPTS_DIR="${ROOT}/scripts"
readonly FIXTURE="${SCRIPT_DIR}/fixtures/rest_shapes.hpp"
readonly DOC_GOLDEN="${SCRIPT_DIR}/golden/rest_shapes.doc.txt.golden"
readonly LINT_GOLDEN="${SCRIPT_DIR}/golden/rest_shapes.lint.txt.golden"
readonly REGEN="${REGEN:-0}"

WORK="$(mktemp -d)"
trap 'rm -rf "${WORK}"' EXIT
readonly JSON="${WORK}/rest_shapes.json"

fail=0

# --- generate the IR -----------------------------------------------------------------
# An empty file here would make both goldens vacuously match under REGEN, the
# same trap run_golden.sh documents. Fail loudly on either symptom instead.
if ! "${TOOL}" --emit-cpp-docs-json --module rest_shapes --output "${JSON}" "${FIXTURE}" \
        -- -std=c++20 -nostdinc++ "-I${INCLUDE_DIR}" 2> "${WORK}/tool.err"; then
    echo "FAIL: apiary exited non-zero on ${FIXTURE}" >&2
    cat "${WORK}/tool.err" >&2
    exit 1
fi
if [[ ! -s "${JSON}" ]]; then
    echo "FAIL: apiary produced no docs JSON" >&2
    echo "  tool:    ${TOOL}" >&2
    echo "  include: ${INCLUDE_DIR}  (both must be ABSOLUTE paths)" >&2
    [[ -s "${WORK}/tool.err" ]] && { echo "--- apiary stderr ---" >&2; cat "${WORK}/tool.err" >&2; }
    exit 1
fi

# --- golden 1: the converted doc text -------------------------------------------------
# Rendered as visible-newline records rather than raw text: blank-line placement
# is precisely what breaks reST, and a golden diff has to show it. Absolute paths
# and line numbers are dropped so the golden survives edits elsewhere in the file.
"${PY}" - "${JSON}" > "${WORK}/doc.txt" <<'PY'
import json, sys

with open(sys.argv[1], encoding="utf-8") as fh:
    doc = json.load(fh)


def entities(node, seen=None):
    seen = set() if seen is None else seen
    if id(node) in seen:
        return
    seen.add(id(node))
    if isinstance(node, dict):
        if isinstance(node.get("doc_structured"), dict):
            yield node
        for v in node.values():
            yield from entities(v, seen)
    elif isinstance(node, list):
        for v in node:
            yield from entities(v, seen)


def show(label, text):
    if not (text or "").strip():
        return
    print(f"  {label}:")
    for line in text.split("\n"):
        print(f"    |{line}")


out = []
for e in entities(doc):
    out.append((e.get("qualified_name") or e.get("name") or "<anonymous>", e["doc_structured"]))
for name, ds in sorted(out):
    print(f"### {name}")
    show("brief", ds.get("brief"))
    show("detail", ds.get("detail"))
    show("returns", ds.get("returns"))
    for field in ("params", "tparams", "throws"):
        for entry in ds.get(field) or []:
            show(f"{field[:-1]} {entry.get('name') or '?'}", entry.get("description"))
    print()
PY

# --- golden 2: the markup diagnostics -------------------------------------------------
# Paths are absolute in the IR (they come from the compilation database), so
# reduce them to a basename before diffing.
set +e
"${PY}" "${SCRIPTS_DIR}/apiary_doc_lint.py" --select malformed-rest "${JSON}" 2>/dev/null \
    | sed -E 's|^.*/([^/]+\.hpp):|\1:|' > "${WORK}/lint.txt"
lint_rc=${PIPESTATUS[0]}
set -e

if [[ "${lint_rc}" -ge 2 ]]; then
    echo "FAIL: apiary_doc_lint.py could not run (exit ${lint_rc}); is docutils installed?" >&2
    exit 1
fi

# --- compare (or regenerate) ----------------------------------------------------------
compare() {
    local actual="$1" golden="$2" what="$3"
    if [[ "${REGEN}" == "1" ]]; then
        cp "${actual}" "${golden}"
        echo "REGEN $(basename "${golden}")"
        return 0
    fi
    if ! diff -u "${golden}" "${actual}"; then
        echo "FAIL: ${what} drifted from committed golden" >&2
        fail=1
    fi
}

compare "${WORK}/doc.txt" "${DOC_GOLDEN}" "converted doc text"
compare "${WORK}/lint.txt" "${LINT_GOLDEN}" "malformed-rest diagnostics"

if [[ "${REGEN}" == "1" ]]; then
    # Not a pass: nothing was verified. Say so, so a regen is never mistaken
    # for a green run.
    echo "REGEN complete: 2 golden(s) rewritten. Review 'git diff' before committing."
    exit 0
fi

if [[ "${fail}" -eq 0 ]]; then
    echo "doc_markup: OK"
fi
exit "${fail}"
