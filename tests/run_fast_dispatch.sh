#!/usr/bin/env bash
# ----------------------------------------------------------------------------------------------
# Copyright (c) The Einsums Developers. All rights reserved.
# Licensed under the MIT License. See LICENSE.txt in the project root for license information.
# ----------------------------------------------------------------------------------------------
#
# Single-entry dispatch, run: bind tests/fixtures/fast_dispatch.hpp under pybind11, link the
# extension, and check from Python that each dispatched name behaves as its APIARY_NO_FAST_DISPATCH
# twin does on pybind11's ordered search: the same overload reached, the same conversions, the same
# error text and docstring.
#
# The golden says the dispatcher's bytes did not change and the compile check says they compile;
# neither says the dispatcher picks the overload the ordered search would, which is its whole
# contract.
#
# usage: run_fast_dispatch.sh <apiary> <include-dir> <python> <cxx> <python-include> <pybind11-include>

set -euo pipefail

if [[ $# -ne 6 ]]; then
    echo "usage: $0 <apiary> <include-dir> <python> <cxx> <python-include> <pybind11-include>" >&2
    exit 64
fi

readonly TOOL="$1"
readonly INCLUDE_DIR="$2"
readonly PY="$3"
readonly CXX_="$4"
readonly PY_INCLUDE="$5"
readonly PYBIND_INCLUDE="$6"
readonly SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly FIXTURE_DIR="${SCRIPT_DIR}/fixtures"

WORK="$(mktemp -d)"
trap 'rm -rf "${WORK}"' EXIT

# From the source tree, so the emitter finds the project's .clang-format (see run_golden.sh).
cd "${SCRIPT_DIR}"
{
    echo '#include "fast_dispatch.hpp"'
    "${TOOL}" --target pybind11 --module fixture_fast_dispatch "${FIXTURE_DIR}/fast_dispatch.hpp" \
        -- -std=c++20 -nostdinc++ "-I${INCLUDE_DIR}"
} > "${WORK}/module.cpp"

suffix="$("${PY}" -c "import sysconfig; print(sysconfig.get_config_var('EXT_SUFFIX'))")"
link=(-shared)
if [[ "$(uname -s)" == Darwin ]]; then
    link+=(-undefined dynamic_lookup)
fi
"${CXX_}" -std=c++20 -O1 -fPIC "${link[@]}" -Werror -Wall -Wextra \
    "-I${FIXTURE_DIR}" "-I${INCLUDE_DIR}" "-I${PY_INCLUDE}" "-I${PYBIND_INCLUDE}" \
    "${WORK}/module.cpp" -o "${WORK}/fixture_fast_dispatch${suffix}"

"${PY}" "${SCRIPT_DIR}/fast_dispatch_check.py" "${WORK}"
