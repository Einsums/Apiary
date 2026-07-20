#!/usr/bin/env bash
# ----------------------------------------------------------------------------------------------
# Copyright (c) The Einsums Developers. All rights reserved.
# Licensed under the MIT License. See LICENSE.txt in the project root for license information.
# ----------------------------------------------------------------------------------------------
#
# Builds examples/ against an *installed* Apiary, exactly as a downstream
# consumer would. This is the only coverage of the consumer-facing helper
# surface - apiary_detect_toolchain -> apiary_add_bindings ->
# apiary_aggregate_extension - which the in-tree suites never touch: they drive
# the binary directly and so exercise none of the CMake plumbing, the install
# tree, or the generated extension at runtime.
#
# Both examples are built and then *imported*, because a green build proves
# nothing about whether the extension actually loads.
#
# Invocation:
#     run_examples.sh <source-dir> <build-dir> <cmake> <python> <generator> <build-type>

set -euo pipefail

if [[ $# -ne 6 ]]; then
    echo "usage: $0 <source-dir> <build-dir> <cmake> <python> <generator> <build-type>" >&2
    exit 64
fi

readonly SRC="$1"
readonly BUILD="$2"
readonly CMAKE="$3"
readonly PY="$4"
readonly GENERATOR="$5"
readonly BUILD_TYPE="$6"

WORK="$(mktemp -d)"
trap 'rm -rf "${WORK}"' EXIT

fail() { echo "FAIL: $*" >&2; exit 1; }

# A build failure here is usually about a file the codegen was expected to
# produce and didn't, and the ninja log alone never shows what actually landed
# on disk. Dump the generated tree so the next CI run carries its own evidence.
dump_tree() {
    local dir="$1"
    echo "--- generated tree under ${dir} ---" >&2
    find "${dir}" \( -name '*.pyi' -o -name '*_pybind*.cpp' -o -name '*.docs.json' \) \
        -printf '%p (%s bytes)\n' 2>/dev/null \
        || find "${dir}" \( -name '*.pyi' -o -name '*_pybind*.cpp' -o -name '*.docs.json' \) 2>/dev/null \
        || echo "(nothing matched)" >&2
}

# Under Git Bash the shell's POSIX view (/tmp/...) is not what a native cmake.exe
# or python.exe understands. MSYS rewrites path-looking *arguments*, but never
# environment variables, so PYTHONPATH below would silently point nowhere.
# cygpath -m yields the C:/... form both tools accept; elsewhere this is a no-op.
native() {
    if command -v cygpath >/dev/null 2>&1; then cygpath -m "$1"; else printf '%s' "$1"; fi
}

readonly PREFIX="${WORK}/prefix"

# --- Install Apiary the way a consumer would consume it -------------------
"${CMAKE}" --install "${BUILD}" --prefix "$(native "${PREFIX}")" >/dev/null \
    || fail "install failed"

# find_package(pybind11 CONFIG) needs the dir shipped inside the wheel.
pybind11_dir="$("${PY}" -c 'import pybind11; print(pybind11.get_cmake_dir())')" \
    || fail "pybind11 not importable"

# --- Each example: configure, build, import -------------------------------
# name | test script | extra build target ("" for none)
readonly CASES=(
    "greeter|test_greeter.py|"
    "mathx|test_mathx.py|mathx_docs"
)

for case in "${CASES[@]}"; do
    IFS='|' read -r name script docs_target <<<"${case}"
    ex="${SRC}/examples/${name}"
    bin="${WORK}/build-${name}"

    "${CMAKE}" -S "$(native "${ex}")" -B "$(native "${bin}")" \
        -G "${GENERATOR}" \
        -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
        -DCMAKE_PREFIX_PATH="$(native "${PREFIX}");${pybind11_dir}" \
        -DPython_EXECUTABLE="${PY}" \
        >"${WORK}/${name}.configure.log" 2>&1 \
        || { cat "${WORK}/${name}.configure.log" >&2; fail "${name}: configure"; }

    "${CMAKE}" --build "$(native "${bin}")" \
        >"${WORK}/${name}.build.log" 2>&1 \
        || { cat "${WORK}/${name}.build.log" >&2; dump_tree "${bin}"; fail "${name}: build"; }

    # A built extension that cannot be imported is still a broken example.
    PYTHONPATH="$(native "${bin}")" "${PY}" "${ex}/${script}" \
        >"${WORK}/${name}.run.log" 2>&1 \
        || { cat "${WORK}/${name}.run.log" >&2; fail "${name}: import/run"; }
    grep -q "OK" "${WORK}/${name}.run.log" \
        || { cat "${WORK}/${name}.run.log" >&2; fail "${name}: test did not report OK"; }

    if [[ -n "${docs_target}" ]]; then
        "${CMAKE}" --build "$(native "${bin}")" --target "${docs_target}" \
            >"${WORK}/${name}.docs.log" 2>&1 \
            || { cat "${WORK}/${name}.docs.log" >&2; fail "${name}: ${docs_target}"; }
        [[ -f "${bin}/docs/index.rst" ]] || fail "${name}: ${docs_target} wrote no index.rst"
    fi

    echo "ok: ${name} built against the install tree and imported cleanly"
done
