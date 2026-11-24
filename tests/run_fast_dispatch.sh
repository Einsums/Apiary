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
# The extension is built through CMake with pybind11_add_module, with the generator and compilers
# the suite was configured with, so each platform links it the way a real project would. Warnings
# are errors under GCC and Clang: the helpers apiary writes into every pybind11 TU must compile
# clean there.
#
# usage: run_fast_dispatch.sh <apiary> <include-dir> <python> <cmake> <generator> <build-type> <c> <cxx>

set -euo pipefail

if [[ $# -ne 8 ]]; then
    echo "usage: $0 <apiary> <include-dir> <python> <cmake> <generator> <build-type> <c> <cxx>" >&2
    exit 64
fi

readonly TOOL="$1"
readonly INCLUDE_DIR="$2"
readonly PY="$3"
readonly CMAKE="$4"
readonly GENERATOR="$5"
readonly BUILD_TYPE="${6:-Release}"
readonly CC_="$7"
readonly CXX_="$8"
readonly SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly FIXTURE_DIR="${SCRIPT_DIR}/fixtures"

native() {
    if command -v cygpath >/dev/null 2>&1; then cygpath -m "$1"; else printf '%s' "$1"; fi
}

WORK="$(mktemp -d)"
trap 'rm -rf "${WORK}"' EXIT

# From the source tree, so the emitter finds the project's .clang-format (see run_golden.sh).
cd "${SCRIPT_DIR}"
{
    echo '#include "fast_dispatch.hpp"'
    "${TOOL}" --target pybind11 --module fixture_fast_dispatch "${FIXTURE_DIR}/fast_dispatch.hpp" \
        -- -std=c++20 -nostdinc++ "-I${INCLUDE_DIR}"
} > "${WORK}/module.cpp"

cat > "${WORK}/CMakeLists.txt" <<CMAKE
cmake_minimum_required(VERSION 3.20)
project(apiary_fast_dispatch_check LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Python COMPONENTS Interpreter Development.Module REQUIRED)
find_package(pybind11 CONFIG REQUIRED)
pybind11_add_module(fixture_fast_dispatch module.cpp)
target_include_directories(fixture_fast_dispatch PRIVATE "$(native "${FIXTURE_DIR}")" "$(native "${INCLUDE_DIR}")")
if(NOT MSVC AND CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "GNU")
    target_compile_options(fixture_fast_dispatch PRIVATE -Wall -Wextra -Werror)
endif()
# One directory whatever the generator, so the check knows where to import from.
set_target_properties(fixture_fast_dispatch PROPERTIES LIBRARY_OUTPUT_DIRECTORY "\$<1:\${CMAKE_BINARY_DIR}/out>")
CMAKE

backend_dir="$("${PY}" -c 'import pybind11; print(pybind11.get_cmake_dir())')"
"${CMAKE}" -S "$(native "${WORK}")" -B "$(native "${WORK}/build")" -G "${GENERATOR}" \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" -DCMAKE_C_COMPILER="${CC_}" -DCMAKE_CXX_COMPILER="${CXX_}" \
    -DCMAKE_PREFIX_PATH="${backend_dir}" -DPython_EXECUTABLE="${PY}" > "${WORK}/configure.log" 2>&1 \
    || { cat "${WORK}/configure.log" >&2; exit 1; }
"${CMAKE}" --build "$(native "${WORK}/build")" --config "${BUILD_TYPE}"

"${PY}" "$(native "${SCRIPT_DIR}/fast_dispatch_check.py")" "$(native "${WORK}/build/out")"
