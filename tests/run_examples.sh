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
# Every example is built and then *imported*, because a green build proves
# nothing about whether the extension actually loads.
#
# The optional ninth argument picks the binding library the examples are built
# against (APIARY_BACKEND); each example's test script is the same under both,
# so the two runs hold pybind11 and nanobind to one behavior. An optional tenth
# argument of STABLE_ABI builds them against the stable ABI (APIARY_STABLE_ABI).
#
# Invocation:
#     run_examples.sh <source-dir> <build-dir> <cmake> <python> <generator> \
#                     <build-type> <c-compiler> <cxx-compiler> [pybind11|nanobind] \
#                     [STABLE_ABI]

set -euo pipefail

if [[ $# -lt 8 || $# -gt 10 ]]; then
    echo "usage: $0 <source-dir> <build-dir> <cmake> <python> <generator>" \
         "<build-type> <c-compiler> <cxx-compiler> [pybind11|nanobind] [STABLE_ABI]" >&2
    exit 64
fi

readonly SRC="$1"
readonly BUILD="$2"
readonly CMAKE="$3"
readonly PY="$4"
readonly GENERATOR="$5"
readonly BUILD_TYPE="$6"
readonly CC_="$7"
readonly CXX_="$8"
readonly BACKEND="${9:-pybind11}"
readonly ABI="${10:-}"
case "${ABI}" in
    "") readonly STABLE_ABI=OFF ;;
    STABLE_ABI) readonly STABLE_ABI=ON ;;
    *) echo "unknown ABI '${ABI}' (expected STABLE_ABI or nothing)" >&2; exit 64 ;;
esac

# A stable-ABI build is only one on Python 3.12 and newer: nanobind ignores
# STABLE_ABI below that and builds an ordinary extension, which would pass every
# check below without testing what it claims to.
EXPECT_ABI3=0
if [[ "${STABLE_ABI}" == ON ]] \
    && "${PY}" -c 'import sys; sys.exit(0 if sys.version_info >= (3, 12) else 1)'; then
    EXPECT_ABI3=1
fi

WORK="$(mktemp -d)"
trap 'rm -rf "${WORK}"' EXIT

fail() { echo "FAIL [${BACKEND}]: $*" >&2; exit 1; }

# The generated TU's name and the class_ spelling it must contain, per backend.
case "${BACKEND}" in
    pybind11) readonly TU_INFIX="_pybind" CLASS_SPELLING="py::class_" ;;
    nanobind) readonly TU_INFIX="_nanobind" CLASS_SPELLING="nb::class_" ;;
    *) echo "unknown backend '${BACKEND}' (expected pybind11 or nanobind)" >&2; exit 64 ;;
esac

# A build failure here is usually about a file the codegen was expected to
# produce and didn't, and the ninja log alone never shows what actually landed
# on disk. Dump the generated tree so the next CI run carries its own evidence.
dump_tree() {
    local dir="$1"
    echo "--- generated tree under ${dir} ---" >&2
    find "${dir}" \( -name '*.pyi' -o -name "*${TU_INFIX}*.cpp" -o -name '*.docs.json' \) \
        -printf '%p (%s bytes)\n' 2>/dev/null \
        || find "${dir}" \( -name '*.pyi' -o -name "*${TU_INFIX}*.cpp" -o -name '*.docs.json' \) 2>/dev/null \
        || echo "(nothing matched)" >&2
    # Sizes alone cannot distinguish "apiary bound nothing" from "apiary bound
    # the wrong thing", and the generated files are small. Show them.
    local f
    for f in $(find "${dir}" -name "*${TU_INFIX}*.cpp" -o -name '*.pyi' 2>/dev/null); do
        echo "--- ${f} ---" >&2
        cat "${f}" >&2
    done
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

# find_package(pybind11 CONFIG) / find_package(nanobind CONFIG) need the CMake
# dir shipped inside the Python package.
if [[ "${BACKEND}" == nanobind ]]; then
    backend_dir="$("${PY}" -c 'import nanobind; print(nanobind.cmake_dir())')" \
        || fail "nanobind not importable"
else
    backend_dir="$("${PY}" -c 'import pybind11; print(pybind11.get_cmake_dir())')" \
        || fail "pybind11 not importable"
fi

# --- Each example: configure, build, import -------------------------------
# name | test script | extra build target ("" for none)
readonly CASES=(
    "greeter|test_greeter.py|"
    "mathx|test_mathx.py|mathx_docs"
    "grid|test_grid.py|"
)

for case in "${CASES[@]}"; do
    IFS='|' read -r name script docs_target <<<"${case}"
    ex="${SRC}/examples/${name}"
    bin="${WORK}/build-${name}"

    # The compiler must be inherited, not left to CMake's default. Apiary probes
    # CMAKE_CXX_COMPILER for the include paths libtooling needs, and a compiler
    # it cannot probe (MSVC cl.exe, whose driver emits no search-list banner)
    # yields an empty include set - so the headers parse to nothing and apiary
    # emits a *valid but empty* module that still compiles and links. Letting the
    # example pick its own compiler silently tested nothing on Windows.
    "${CMAKE}" -S "$(native "${ex}")" -B "$(native "${bin}")" \
        -G "${GENERATOR}" \
        -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
        -DCMAKE_C_COMPILER="${CC_}" \
        -DCMAKE_CXX_COMPILER="${CXX_}" \
        -DCMAKE_PREFIX_PATH="$(native "${PREFIX}");${backend_dir}" \
        -DPython_EXECUTABLE="${PY}" \
        -DAPIARY_BACKEND="${BACKEND}" \
        -DAPIARY_STABLE_ABI="${STABLE_ABI}" \
        >"${WORK}/${name}.configure.log" 2>&1 \
        || { cat "${WORK}/${name}.configure.log" >&2; fail "${name}: configure"; }

    # --verbose so a failure log carries the actual apiary invocation. What
    # flags the codegen ran with is the first thing anyone needs, and ninja's
    # default one-line-per-edge output hides exactly that.
    "${CMAKE}" --build "$(native "${bin}")" --verbose \
        >"${WORK}/${name}.build.log" 2>&1 \
        || {
            # The configure log holds apiary_detect_toolchain's probe results
            # (resource-dir, C++ include dirs). A build failure is very often a
            # probe that came back empty, so surface it here too.
            echo "--- ${name} configure log ---" >&2
            grep -E "apiary:|Found Python|pybind11|nanobind" "${WORK}/${name}.configure.log" >&2 || true
            cat "${WORK}/${name}.build.log" >&2
            dump_tree "${bin}"
            fail "${name}: build"
        }

    # Apiary emits a structurally valid but *empty* module when the parse finds
    # no declarations - which compiles, links, and imports, so every downstream
    # check passes while binding nothing. Assert the generated TU actually
    # contains bindings, or a broken toolchain probe looks like success.
    tu="$(find "${bin}" -name "*${TU_INFIX}*.cpp" | head -1)"
    [[ -n "${tu}" ]] || fail "${name}: no binding TU generated"
    grep -qE "\.def|${CLASS_SPELLING}" "${tu}" \
        || { dump_tree "${bin}"; fail "${name}: generated TU binds nothing (empty module)"; }

    if (( EXPECT_ABI3 )); then
        core="$(find "${bin}/${name}" -maxdepth 1 -name '_core.*' ! -name '*.pyi' | head -1)"
        [[ "${core}" == *.abi3.* ]] || fail "${name}: STABLE_ABI requested, but the extension is ${core:-missing}"
    fi

    # A built extension that cannot be imported is still a broken example.
    PYTHONPATH="$(native "${bin}")" "${PY}" "${ex}/${script}" \
        >"${WORK}/${name}.run.log" 2>&1 \
        || { cat "${WORK}/${name}.run.log" >&2; fail "${name}: import/run"; }
    grep -q "OK" "${WORK}/${name}.run.log" \
        || { cat "${WORK}/${name}.run.log" >&2; fail "${name}: test did not report OK"; }

    # The stubs sit in the package, beside the _core they describe, where a
    # type checker looks for ``import greeter``.
    if [[ "${name}" == greeter ]]; then
        # greeter._extras is hand-written against <apiary/Python.hpp>; it must
        # have been built with the library this run asked for, which is the
        # define apiary_add_extension_module set.
        built_with="$(PYTHONPATH="$(native "${bin}")" "${PY}" -c 'import greeter._extras as e; print(e.backend)')"
        [[ "${built_with}" == "${BACKEND}" ]] || fail "greeter: _extras was built with '${built_with}', not ${BACKEND}"
        for want in "class Greeter:|${bin}/greeter/_core.pyi" "class GreetingError\(Exception\)|${bin}/greeter/_core.pyi" \
                    "^from \._core import \*|${bin}/greeter/__init__.pyi"; do
            pattern="${want%|*}" file="${want##*|}"
            [[ -f "${file}" ]] || fail "greeter: no stub at ${file}"
            grep -qE -- "${pattern}" "${file}" || { cat "${file}" >&2; fail "greeter: ${file} lacks ${pattern}"; }
        done
    fi

    # mathx's stub must carry what its hand-written __init__.py adds, or a
    # type checker, which reads __init__.pyi instead, never sees it.
    if [[ "${name}" == mathx ]]; then
        init_pyi="${bin}/mathx/__init__.pyi"
        for want in '^def version\(\) -> str:' '^from \.extras import lerp as lerp, normalize as normalize$'; do
            grep -qE -- "${want}" "${init_pyi}" || { cat "${init_pyi}" >&2; fail "mathx: __init__.pyi lacks ${want}"; }
        done
    fi

    if [[ -n "${docs_target}" ]]; then
        "${CMAKE}" --build "$(native "${bin}")" --target "${docs_target}" \
            >"${WORK}/${name}.docs.log" 2>&1 \
            || { cat "${WORK}/${name}.docs.log" >&2; fail "${name}: ${docs_target}"; }
        [[ -f "${bin}/docs/index.rst" ]] || fail "${name}: ${docs_target} wrote no index.rst"
    fi

    echo "ok: ${name} (${BACKEND}${ABI:+, ${ABI}}) built against the install tree and imported cleanly"
done
