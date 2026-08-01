#!/usr/bin/env bash
# ----------------------------------------------------------------------------------------------
# Copyright (c) The Einsums Developers. All rights reserved.
# Licensed under the MIT License. See LICENSE.txt in the project root for license information.
# ----------------------------------------------------------------------------------------------
#
# Diagnostics suite. The emitters can now say "I could not represent this";
# this asserts that they DO, that the message names the header, and that the
# severity policy behaves.
#
# The diagnostics are goldened rather than grepped because their wording is the
# product: a diagnostic that does not say what is wrong and what it costs is
# barely better than silence, and wording rots quietly.
#
# The fixtures used here are the same ones that pin the broken OUTPUT
# (qualifiers.hpp, variadic_pack.hpp). That pairing is deliberate: when one of
# those defects is fixed, its golden output changes AND its diagnostic leaves
# this golden, so neither can be updated while forgetting the other.
#
# Updating the golden: run with REGEN=1.
#
# Invocation:
#     run_diagnostics.sh <apiary-binary> <annotations-include-dir>

set -euo pipefail

if [[ $# -ne 2 ]]; then
    echo "usage: $0 <apiary-binary> <annotations-include-dir>" >&2
    exit 64
fi

readonly TOOL="$1"
readonly INCLUDE_DIR="$2"
readonly SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly FIXTURE_DIR="${SCRIPT_DIR}/fixtures"
readonly GOLDEN="${SCRIPT_DIR}/golden/diagnostics.txt.golden"
readonly REGEN="${REGEN:-0}"

WORK="$(mktemp -d)"
trap 'rm -rf "${WORK}"' EXIT

fail=0

# Run the tool over one fixture and keep only apiary's own diagnostics.
# Absolute paths are reduced to a basename so the golden survives a move, and
# the "N binding statement(s)" progress line is dropped - it belongs to the
# emitter's reporting, not to this suite.
collect() {
    local fixture="$1"
    shift
    # A case that reports NOTHING is a real expected outcome here (a silenced
    # check, a clean fixture), and `grep -v` exits 1 when it selects no lines -
    # which under `set -e -o pipefail` would abort the run and look like a tool
    # failure. Swallow just that.
    "${TOOL}" --module diag_fixture "$@" "${FIXTURE_DIR}/${fixture}" \
        -- -std=c++20 -nostdinc++ "-I${INCLUDE_DIR}" 2>&1 >/dev/null |
        { grep -v "^apiary: module " || true; } |
        sed -E 's|^.*/([^/]+\.hpp):|\1:|'
}

{
    echo "### qualifiers.hpp (rvalue-qualified methods)"
    collect qualifiers.hpp
    echo
    # Empty since packs expand: the section stays as the regression guard that
    # they still do.
    echo "### variadic_pack.hpp (parameter packs - expanded, so silent)"
    collect variadic_pack.hpp
    echo
    echo "### skipped-entity, opt-in (default is ignored)"
    collect rest_shapes.hpp --diagnostic=skipped-entity=note
    echo
    echo "### a check silenced"
    collect qualifiers.hpp --diagnostic=moved-from-self=ignored
} > "${WORK}/actual.txt"

if [[ "${REGEN}" == "1" ]]; then
    cp "${WORK}/actual.txt" "${GOLDEN}"
    echo "REGEN $(basename "${GOLDEN}")"
else
    if ! diff -u "${GOLDEN}" "${WORK}/actual.txt"; then
        echo "FAIL: diagnostics drifted from committed golden" >&2
        fail=1
    fi
fi

# --- severity policy --------------------------------------------------------
# Warnings alone must not fail a run: the tool has always emitted for these
# inputs, and turning that into a hard failure is the project's choice to make.
if "${TOOL}" --module diag_fixture "${FIXTURE_DIR}/qualifiers.hpp" \
        -- -std=c++20 -nostdinc++ "-I${INCLUDE_DIR}" >/dev/null 2>&1; then
    : # exit 0 as expected
else
    echo "FAIL: warnings alone should not change the exit status" >&2
    fail=1
fi

# ...and --werror is how a project ratchets.
if "${TOOL}" --module diag_fixture --werror "${FIXTURE_DIR}/qualifiers.hpp" \
        -- -std=c++20 -nostdinc++ "-I${INCLUDE_DIR}" >/dev/null 2>&1; then
    echo "FAIL: --werror should fail the run when a warning was reported" >&2
    fail=1
fi

# A clean fixture stays clean under --werror, or the ratchet is unusable.
if ! "${TOOL}" --module diag_fixture --werror "${FIXTURE_DIR}/simple_class.hpp" \
        -- -std=c++20 -nostdinc++ "-I${INCLUDE_DIR}" >/dev/null 2>&1; then
    echo "FAIL: a clean fixture must pass --werror" >&2
    fail=1
fi

# --- option validation ------------------------------------------------------
# A typo in --diagnostic must be refused BEFORE any output is written; silently
# running under a policy the caller did not ask for is the failure this whole
# feature exists to remove.
for bad in "nope=error" "unrepresentable-pack=nope" "missing-equals"; do
    "${TOOL}" --module diag_fixture "--diagnostic=${bad}" "${FIXTURE_DIR}/simple_class.hpp" \
        -- -std=c++20 -nostdinc++ "-I${INCLUDE_DIR}" >/dev/null 2>&1 && rc=0 || rc=$?
    if [[ "${rc}" -ne 2 ]]; then
        echo "FAIL: --diagnostic=${bad} should exit 2, got ${rc}" >&2
        fail=1
    fi
done

# --- nothing is written until the exit status is known -----------------------
# Emission is where the emitters report what they could not represent, so the
# status is not knowable until every artifact exists - but it IS knowable before
# any of them reaches disk. A failing run that left output behind looked fresh
# to anything that did not check the status.
OUTDIR="${WORK}/out"
mkdir -p "${OUTDIR}"
"${TOOL}" --module diag_fixture --werror \
    --output "${OUTDIR}/gen.cpp" --stub-output "${OUTDIR}/gen.pyi" \
    "${FIXTURE_DIR}/qualifiers.hpp" -- -std=c++20 -nostdinc++ "-I${INCLUDE_DIR}" >/dev/null 2>&1 && rc=0 || rc=$?
if [[ "${rc}" -ne 1 ]]; then
    echo "FAIL: a --werror run with warnings should exit 1, got ${rc}" >&2
    fail=1
fi
if [[ -e "${OUTDIR}/gen.cpp" || -e "${OUTDIR}/gen.pyi" ]]; then
    echo "FAIL: a failing run left generated output on disk" >&2
    ls -la "${OUTDIR}" >&2
    fail=1
fi
# ...and a passing run still writes everything it was asked for.
if ! "${TOOL}" --module diag_fixture --werror \
        --output "${OUTDIR}/ok.cpp" --stub-output "${OUTDIR}/ok.pyi" \
        "${FIXTURE_DIR}/simple_class.hpp" -- -std=c++20 -nostdinc++ "-I${INCLUDE_DIR}" >/dev/null 2>&1; then
    echo "FAIL: a clean run should succeed" >&2
    fail=1
fi
for f in "${OUTDIR}/ok.cpp" "${OUTDIR}/ok.pyi"; do
    if [[ ! -s "${f}" ]]; then
        echo "FAIL: a passing run did not write ${f}" >&2
        fail=1
    fi
done

# --list-diagnostics must work without naming a source file.
if ! "${TOOL}" --list-diagnostics 2>/dev/null | grep -q "moved-from-self"; then
    echo "FAIL: --list-diagnostics should list the checks without a source file" >&2
    fail=1
fi

if [[ "${REGEN}" == "1" ]]; then
    echo "REGEN complete: 1 golden rewritten. Review 'git diff' before committing."
    exit 0
fi

if [[ "${fail}" -eq 0 ]]; then
    echo "diagnostics: OK"
fi
exit "${fail}"
