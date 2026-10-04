#!/usr/bin/env bash
# ----------------------------------------------------------------------------------------------
# Copyright (c) The Einsums Developers. All rights reserved.
# Licensed under the MIT License. See LICENSE.txt in the project root for license information.
# ----------------------------------------------------------------------------------------------
#
# Removes modules from a consumer's build and rebuilds the same tree, as a
# downstream project does when it drops a module. The generated .pyi stubs and
# the rendered docs must then describe the extension that was built, not the
# union of every module the tree has ever had. A fresh tree is always right;
# only an incremental one can hold on to a removed module's output, so that is
# the only kind this test builds.
#
# The consumer is tests/fixtures/module_removal, built against an *installed*
# Apiary like the examples (tests/run_examples.sh).
#
# Invocation:
#     run_module_removal.sh <source-dir> <build-dir> <cmake> <python> <generator> \
#                           <build-type> <c-compiler> <cxx-compiler>

set -euo pipefail

if [[ $# -ne 8 ]]; then
    echo "usage: $0 <source-dir> <build-dir> <cmake> <python> <generator>" \
         "<build-type> <c-compiler> <cxx-compiler>" >&2
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

WORK="$(mktemp -d)"
trap 'rm -rf "${WORK}"' EXIT

fail() { echo "FAIL: $*" >&2; exit 1; }
assert_contains() { grep -qE -- "$2" "$1" || { echo "--- $1 ---" >&2; cat "$1" >&2; fail "expected in $1: $2"; }; }
assert_absent()   { ! grep -qE -- "$2" "$1" || { echo "--- $1 ---" >&2; cat "$1" >&2; fail "forbidden in $1: $2"; }; }

# See tests/run_examples.sh: native cmake/python under Git Bash need C:/ paths.
native() {
    if command -v cygpath >/dev/null 2>&1; then cygpath -m "$1"; else printf '%s' "$1"; fi
}

readonly PREFIX="${WORK}/prefix"
readonly FIXTURE="${SRC}/tests/fixtures/module_removal"
readonly BIN="${WORK}/build"
readonly PKG="${BIN}/demo"
readonly DOCS="${BIN}/docs"

"${CMAKE}" --install "${BUILD}" --prefix "$(native "${PREFIX}")" >/dev/null || fail "install failed"
pybind11_dir="$("${PY}" -c 'import pybind11; print(pybind11.get_cmake_dir())')" \
    || fail "pybind11 not importable"

configure() {
    "${CMAKE}" -S "$(native "${FIXTURE}")" -B "$(native "${BIN}")" \
        -G "${GENERATOR}" \
        -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
        -DCMAKE_C_COMPILER="${CC_}" \
        -DCMAKE_CXX_COMPILER="${CXX_}" \
        -DCMAKE_PREFIX_PATH="$(native "${PREFIX}");${pybind11_dir}" \
        -DPython_EXECUTABLE="${PY}" \
        "$@" >"${WORK}/configure.log" 2>&1 \
        || { cat "${WORK}/configure.log" >&2; fail "configure $*"; }
}

build() {
    "${CMAKE}" --build "$(native "${BIN}")" >"${WORK}/build.log" 2>&1 \
        && "${CMAKE}" --build "$(native "${BIN}")" --target demo_docs >>"${WORK}/build.log" 2>&1 \
        || { cat "${WORK}/build.log" >&2; fail "build"; }
}

# The names the built extension actually has, for comparing against its stubs.
runtime_names() {
    PYTHONPATH="$(native "${BIN}")" "${PY}" -c \
        'import demo, demo._core as c; print(sorted(n for n in dir(c) if not n.startswith("_")))'
}

extension() { find "${PKG}" -maxdepth 1 -name '_core*' ! -name '*.pyi' | head -1; }
mtime() { "${PY}" -c 'import os, sys; print(os.stat(sys.argv[1]).st_mtime_ns)' "$1"; }

# ── Every module ─────────────────────────────────────────────────────────────
configure -DWITH_GONE=ON -DWITH_SOLO=ON
build
[[ "$(runtime_names)" == "['Gone', 'Kept', 'solo']" ]] || fail "first build: unexpected extension contents $(runtime_names)"
assert_contains "${PKG}/_core.pyi" "^class Gone:"
assert_contains "${PKG}/solo.pyi" "^class Solo:"
assert_contains "${PKG}/__init__.pyi" "^from \. import solo as solo$"
assert_contains "${DOCS}/demo.rst" "py:class:: Gone"
[[ -f "${DOCS}/demo.solo.rst" ]] || fail "first build: no page for the solo submodule"

# ── A reconfigure that changes nothing rebuilds nothing ──────────────────────
# The register header is written at configure time; rewriting identical
# content would recompile the extension's main TU and relink it.
ext="$(extension)"
[[ -n "${ext}" ]] || fail "no _core extension under ${PKG}"
before="$(mtime "${ext}")"
sleep 1
configure -DWITH_GONE=ON -DWITH_SOLO=ON
build
[[ "$(mtime "${ext}")" == "${before}" ]] || fail "an unchanged reconfigure relinked the extension"

# ── Remove two modules from the same tree ────────────────────────────────────
# GNU Make 3.81 (macOS) compares whole seconds, and the reconfigure below
# rewrites the inputs that make the stubs and docs stale. Keep them strictly
# newer than the last build's outputs.
sleep 1
configure -DWITH_GONE=OFF -DWITH_SOLO=OFF
build
[[ "$(runtime_names)" == "['Kept']" ]] || fail "second build: unexpected extension contents $(runtime_names)"
assert_contains "${PKG}/_core.pyi" "^class Kept:"
assert_absent   "${PKG}/_core.pyi" "^class Gone:"
[[ ! -e "${PKG}/solo.pyi" ]] || fail "solo.pyi survived the removal of the only module in it"
assert_absent   "${PKG}/__init__.pyi" "import solo"
assert_contains "${DOCS}/demo.rst" "py:class:: Kept"
assert_absent   "${DOCS}/demo.rst" "py:class:: Gone"
[[ ! -e "${DOCS}/demo.solo.rst" ]] || fail "the solo submodule's page survived its removal"

# ── The stubs describe this package, and only it ─────────────────────────────
# The staged package sits in the build directory, so the aggregator must not
# take it for a helper package: only PY_HELPERS_DIR names those.
[[ ! -e "${PKG}/demo" ]] || fail "the staged package was stubbed as a helper package of itself"
assert_absent   "${PKG}/__init__.pyi" "import demo"

echo "ok: removing modules from a built tree removes their stubs and pages"
