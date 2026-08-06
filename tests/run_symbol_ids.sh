#!/usr/bin/env bash
# ----------------------------------------------------------------------------------------------
# Copyright (c) The Einsums Developers. All rights reserved.
# Licensed under the MIT License. See LICENSE.txt in the project root for license information.
# ----------------------------------------------------------------------------------------------
#
# Symbol-ID stability under an ABI-versioning inline namespace.
#
# A library that wraps itself in `namespace lib { inline namespace vN { ... } }`
# so two copies cannot interpose in one process gets that vN recorded in every
# Clang USR, because a USR records every enclosing DeclContext. The inline
# namespace is transparent to name lookup, so nothing else about the API
# changes - but symbol_id would change for every entity on every ABI bump, and
# so would every memberOf/inheritsFrom/overrides edge built from those IDs.
#
# The invariant is stronger than "no vN appears": an entity's ID must be the
# SAME whether or not the ABI namespace is there at all. So this compares the
# fixture against a copy of itself with the inline namespace deleted, which
# needs no golden and cannot rot into a tautology the way a committed list of
# IDs can.
#
# The fixture is deliberately NOT in the run_golden.sh corpus. Its emitted
# bindings are identical to a plain namespace's - that is assertion 3 below -
# so a golden would pin nothing this file does not, while adding two more
# files to regenerate on every emitter change.
#
# Invocation:
#     run_symbol_ids.sh <apiary-binary> <annotations-include-dir>

set -euo pipefail

if [[ $# -ne 2 ]]; then
    echo "usage: $0 <apiary-binary> <annotations-include-dir>" >&2
    exit 64
fi

readonly TOOL="$1"
readonly INCLUDE_DIR="$2"
readonly SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly FIXTURE="${SCRIPT_DIR}/fixtures/inline_namespace.hpp"

WORK="$(mktemp -d)"
trap 'rm -rf "${WORK}"' EXIT

fail=0

# Same basename in two directories, so the `#include "..."` the emitter writes
# into the generated TU is identical and the outputs can be diffed directly.
mkdir -p "${WORK}/versioned" "${WORK}/plain"
cp "${FIXTURE}" "${WORK}/versioned/inline_namespace.hpp"
sed -e '/^inline namespace FIXTURE_ABI_TAG {$/d' \
    -e '/^} \/\/ namespace FIXTURE_ABI_TAG$/d' \
    "${FIXTURE}" > "${WORK}/plain/inline_namespace.hpp"

# The sed above is load-bearing: if it silently matched nothing, both sides
# would be the versioned fixture and every comparison below would pass while
# testing nothing.
if ! grep -q '^inline namespace FIXTURE_ABI_TAG {$' "${WORK}/versioned/inline_namespace.hpp"; then
    echo "FAIL: fixture no longer opens the inline namespace this test is about" >&2
    exit 1
fi
if grep -q 'FIXTURE_ABI_TAG {' "${WORK}/plain/inline_namespace.hpp"; then
    echo "FAIL: could not strip the inline namespace from the fixture copy" >&2
    exit 1
fi

run() {
    local variant="$1"
    local dir="${WORK}/${variant}"
    # stderr is kept, not discarded: the "N binding statement(s)" progress line
    # is noise in a passing run, but a tool that could not parse at all reports
    # there, and swallowing that would turn a broken run into an empty diff.
    "${TOOL}" --module symbol_id_fixture \
        --source-include inline_namespace.hpp \
        --output "${dir}/gen.cpp" --stub-output "${dir}/gen.pyi" \
        "${dir}/inline_namespace.hpp" -- -std=c++20 -nostdinc++ "-I${INCLUDE_DIR}" \
        >/dev/null 2>> "${WORK}/tool.err"
    "${TOOL}" --emit-cpp-docs-json --module symbol_id_fixture \
        --output "${dir}/docs.json" \
        "${dir}/inline_namespace.hpp" -- -std=c++20 -nostdinc++ "-I${INCLUDE_DIR}" \
        >/dev/null 2>> "${WORK}/tool.err"
    # Every identifier the docs JSON carries: the IDs themselves and both ends
    # of every relation edge.
    grep -oE '"(symbol_id|source|target)": "[^"]*"' "${dir}/docs.json" \
        | grep -v '": ""$' | sort > "${dir}/ids.txt"
}

run versioned
run plain

# --- 1. the ABI tag reaches nothing -----------------------------------------
if grep -q '@N@v7' "${WORK}/versioned/docs.json"; then
    echo "FAIL: the inline namespace leaked into the docs JSON" >&2
    grep -oE '"[a-z_]+": "[^"]*@N@v7[^"]*"' "${WORK}/versioned/docs.json" | sort -u >&2
    fail=1
fi

# --- 2. IDs are identical with and without the ABI namespace ----------------
# Guard first: comparing two empty extractions would "pass" without testing
# anything, which is how a harness quietly stops working.
id_count="$(wc -l < "${WORK}/versioned/ids.txt" | tr -d ' ')"
if (( id_count < 10 )); then
    echo "FAIL: expected the fixture to yield symbol IDs and edges, got ${id_count}" >&2
    fail=1
elif ! diff -u "${WORK}/plain/ids.txt" "${WORK}/versioned/ids.txt"; then
    echo "FAIL: symbol IDs differ between the plain and ABI-versioned fixture" >&2
    fail=1
fi

# --- 3. bindings and stubs are unaffected too -------------------------------
# The inline namespace is transparent to name lookup, so the emitted C++ names
# it and the plain form identically. This is what lets a library adopt an ABI
# namespace without regenerating a single binding.
for artifact in gen.cpp gen.pyi; do
    if ! diff -u "${WORK}/plain/${artifact}" "${WORK}/versioned/${artifact}"; then
        echo "FAIL: ${artifact} differs between the plain and ABI-versioned fixture" >&2
        fail=1
    fi
done

if [[ "${fail}" -ne 0 ]]; then
    echo "--- tool stderr ---" >&2
    cat "${WORK}/tool.err" >&2
else
    echo "symbol ids: OK (${id_count} ids and edges, stable across the ABI namespace)"
fi
exit "${fail}"
