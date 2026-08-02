#!/usr/bin/env bash
# ----------------------------------------------------------------------------------------------
# Copyright (c) The Einsums Developers. All rights reserved.
# Licensed under the MIT License. See LICENSE.txt in the project root for license information.
# ----------------------------------------------------------------------------------------------
#
# Fuzz suite. Generates random-but-valid annotated headers and holds each one to
# the three assertions that already exist for the fixture corpus:
#
#   1. apiary emits without error;
#   2. the generated binding TU COMPILES;
#   3. the generated stub PARSES as Python, with SyntaxWarning fatal;
#
# The fixture corpus contains what someone thought to write down, which is
# exactly why the shapes nobody thought of are the ones still broken. The
# generator emits only constructs apiary claims to support - it just combines
# them in ways no one would bother to enumerate - so anything it breaks is a
# real defect on documented input.
#
# Determinism matters more here than anywhere else in the suite: a fuzz failure
# you cannot reproduce is a rumour. ctest runs a FIXED seed so the suite is
# stable and a red run is always reproducible; pass a different one by hand to
# search:
#
#     APIARY_FUZZ_SEED=12345 APIARY_FUZZ_COUNT=200 run_fuzz.sh ...
#
# A failure prints the seed, the index, and the offending header.
#
# Invocation:
#     run_fuzz.sh <apiary-binary> <include-dir> <python> <c++> <pybind11-include> <python-include>

set -euo pipefail

if [[ $# -ne 6 ]]; then
    echo "usage: $0 <apiary-binary> <include-dir> <python> <cxx> <pybind11-include> <python-include>" >&2
    exit 64
fi

readonly TOOL="$1"
readonly INCLUDE_DIR="$2"
readonly PY="$3"
readonly CXX="$4"
readonly PYBIND_INCLUDE="$5"
readonly PYTHON_INCLUDE="$6"
readonly SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# shellcheck source=tests/cxx_driver.sh
. "${SCRIPT_DIR}/cxx_driver.sh"
apiary_set_cxx_syntax_flags "${CXX}"

readonly SEED="${APIARY_FUZZ_SEED:-20260801}"
readonly COUNT="${APIARY_FUZZ_COUNT:-25}"

WORK="$(mktemp -d)"
trap 'rm -rf "${WORK}"' EXIT

fail=0

report() {
    local index="$1" what="$2" detail="$3" header="$4"
    echo "FAIL seed=${SEED} index=${index}: ${what}" >&2
    echo "  reproduce: python tests/generate_headers.py --seed ${SEED} --index ${index}" >&2
    [[ -n "${detail}" ]] && sed 's/^/  /' <<<"${detail}" >&2
    echo "--- generated header ---" >&2
    sed 's/^/  /' "${header}" >&2
    fail=1
}

for (( i = 0; i < COUNT; i++ )); do
    header="${WORK}/fuzz_${i}.hpp"
    "${PY}" "${SCRIPT_DIR}/generate_headers.py" --seed "${SEED}" --index "${i}" -o "${header}"

    # 0. The generated HEADER must itself be valid C++. Without this the
    #    harness can accuse apiary of a defect that is really a bug in the
    #    grammar - which happened immediately, with an over-escaped character
    #    literal. A fuzzer that cries wolf gets switched off.
    herr="${WORK}/fuzz_${i}.hdr.err"
    if ! "${CXX}" "${APIARY_CXX_SYNTAX_FLAGS[@]}" -Wno-unused -I"${INCLUDE_DIR}" -x c++ "${header}" 2> "${herr}"; then
        echo "FAIL seed=${SEED} index=${i}: the GENERATED HEADER is not valid C++ - this is a generator bug" >&2
        sed 's/^/  /' "${herr}" >&2
        fail=1
        continue
    fi

    gen="${WORK}/fuzz_${i}.cpp"
    stub="${WORK}/fuzz_${i}.pyi"
    err="${WORK}/fuzz_${i}.tool.err"

    # 1. apiary must emit. --source-include makes the TU include the header it
    #    binds, which is what a real consumer's build does.
    if ! "${TOOL}" --module fuzz_module --source-include "$(basename "${header}")" \
            --stub-output "${stub}" "${header}" \
            -- -std=c++20 -nostdinc++ "-I${INCLUDE_DIR}" > "${gen}" 2> "${err}"; then
        report "${i}" "apiary exited non-zero" "$(cat "${err}")" "${header}"
        continue
    fi

    # 2. the binding TU must compile.
    cerr="${WORK}/fuzz_${i}.cxx.err"
    if ! "${CXX}" "${APIARY_CXX_SYNTAX_FLAGS[@]}" \
            -I"${PYBIND_INCLUDE}" -I"${PYTHON_INCLUDE}" -I"${WORK}" -I"${INCLUDE_DIR}" \
            "${gen}" 2> "${cerr}"; then
        report "${i}" "the generated TU does not compile" "$(head -25 "${cerr}")" "${header}"
        continue
    fi

    # 3. the stub must parse, with SyntaxWarning fatal - an unknown escape is
    #    only a warning today and Python says it will stop working.
    perr="${WORK}/fuzz_${i}.py.err"
    if ! "${PY}" -W error::SyntaxWarning -c 'import ast,sys; ast.parse(open(sys.argv[1]).read())' "${stub}" 2> "${perr}"; then
        report "${i}" "the generated stub is not valid Python" "$(cat "${perr}")" "${header}"
        continue
    fi
done

if [[ "${fail}" -eq 0 ]]; then
    echo "fuzz: OK (seed ${SEED}, ${COUNT} header(s))"
fi
exit "${fail}"
