#!/usr/bin/env bash
# ----------------------------------------------------------------------------------------------
# Copyright (c) The Einsums Developers. All rights reserved.
# Licensed under the MIT License. See LICENSE.txt in the project root for license information.
# ----------------------------------------------------------------------------------------------
#
# Compile check: the generated binding TU must actually compile.
#
# Every other suite here asks whether the output CHANGED, or whether it parses
# as Python. None of them asks the one question the C++ half exists to answer.
# That gap is not theoretical - two defects in this repo were found by hand-
# compiling an emitted expression, and both had passed every golden:
#
#   * a ref-qualified overload emitted a static_cast that named no overload,
#     because a ref-qualifier is part of a member function's type;
#   * a parameter pack emitted `double...`, which a C++ cast reads as varargs.
#
# Both produced a TU that clang rejects outright, and both were invisible to a
# golden, which only ever said the bytes were the same bytes as last time.
#
# Syntax-only (-fsyntax-only): the fixtures declare their entities without
# defining them, so linking is not possible and not the point. Compiling is
# what proves the emitted casts, lambdas and type names are well-formed.
#
# Invocation:
#     run_compile_check.sh <apiary-binary> <annotations-include-dir> <c++-compiler> <pybind11-include> <python-include>

set -euo pipefail

if [[ $# -ne 5 ]]; then
    echo "usage: $0 <apiary-binary> <include-dir> <cxx> <pybind11-include> <python-include>" >&2
    exit 64
fi

readonly TOOL="$1"
readonly INCLUDE_DIR="$2"
readonly CXX="$3"
readonly PYBIND_INCLUDE="$4"
readonly PYTHON_INCLUDE="$5"
readonly SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly FIXTURE_DIR="${SCRIPT_DIR}/fixtures"

# Fixtures whose emitted TU is KNOWN not to compile, as ``name|reason`` - the
# same shape the golden runners use for their cases, and portable to the bash
# 3.2 macOS ships (no associative arrays, no [[ -v ]]).
#
# Each is a defect the tool reports on (see run_diagnostics.sh) and has not yet
# fixed. They are listed rather than skipped so that fixing one turns this list
# into a failure that says "you can remove me now".
readonly KNOWN_BAD=(
    "variadic_pack.hpp|Finding 10a: a parameter pack emits 'double...', which a C++ cast reads as varargs"
)

# Fixtures that CANNOT be compile-checked by construction - a different thing
# from a defect, and kept in a separate list so the two are never confused.
readonly NOT_COMPILABLE=(
    "std_types.hpp|declares its own std:: stand-ins for hermetic goldens; they collide with the real libc++ that pybind11 pulls in"
)

# Echo the reason this fixture is in the given list, or nothing.
reason_from() {
    local name="$1" entry
    shift
    for entry in "$@"; do
        if [[ "${entry%%|*}" == "${name}" ]]; then
            echo "${entry#*|}"
            return
        fi
    done
}

WORK="$(mktemp -d)"
trap 'rm -rf "${WORK}"' EXIT

fail=0
checked=0
skipped=0

shopt -s nullglob
for fixture in "${FIXTURE_DIR}"/*.hpp; do
    name="$(basename "${fixture}")"
    if [[ -n "$(reason_from "${name}" "${NOT_COMPILABLE[@]}")" ]]; then
        skipped=$((skipped + 1))
        continue
    fi
    gen="${WORK}/${name%.hpp}.cpp"

    # --source-include makes the generated TU include the header that declares
    # what it binds, which is what a real consumer's build does.
    if ! "${TOOL}" --module compile_check --source-include "${name}" "${fixture}" \
            -- -std=c++20 -nostdinc++ "-I${INCLUDE_DIR}" > "${gen}" 2>/dev/null; then
        echo "FAIL ${name}: apiary could not emit a TU" >&2
        fail=1
        continue
    fi

    err="${WORK}/${name}.err"
    if "${CXX}" -std=c++20 -fsyntax-only \
            -I"${PYBIND_INCLUDE}" -I"${PYTHON_INCLUDE}" -I"${FIXTURE_DIR}" -I"${INCLUDE_DIR}" \
            "${gen}" 2> "${err}"; then
        compiled=1
    else
        compiled=0
    fi

    checked=$((checked + 1))
    reason="$(reason_from "${name}" "${KNOWN_BAD[@]}")"
    if [[ -n "${reason}" ]]; then
        if [[ "${compiled}" -eq 1 ]]; then
            echo "FAIL ${name}: listed as known-bad but it compiles now - remove it from KNOWN_BAD" >&2
            echo "  the listed reason was: ${reason}" >&2
            fail=1
        fi
        continue
    fi

    if [[ "${compiled}" -eq 0 ]]; then
        echo "FAIL ${name}: the generated TU does not compile" >&2
        head -30 "${err}" >&2
        fail=1
    fi
done
shopt -u nullglob

if (( checked == 0 )); then
    echo "FAIL: no fixtures were checked" >&2
    exit 1
fi

if [[ "${fail}" -eq 0 ]]; then
    echo "compile_check: OK (${checked} compiled, ${#KNOWN_BAD[@]} known-bad, ${skipped} not compilable)"
fi
exit "${fail}"
