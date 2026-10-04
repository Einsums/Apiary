#!/usr/bin/env bash
# ----------------------------------------------------------------------------------------------
# Copyright (c) The Einsums Developers. All rights reserved.
# Licensed under the MIT License. See LICENSE.txt in the project root for license information.
# ----------------------------------------------------------------------------------------------
#
# Which Clang builtin headers apiary_detect_toolchain() hands apiary.
#
# They must be the ones for apiary's own libclang: the intrinsics headers call
# builtins by name, and another major's can name one it does not have. So the
# helper takes apiary's own when apiary reports them, borrows a Clang's only
# when it does not (warning when that Clang's major differs), and says so when
# it finds none. Where the compiler's whole search list is forwarded (macOS,
# Windows), its builtin directory keeps its place, ahead of the SDK headers
# that #include_next past it, but holds apiary's headers instead.
#
# The apiary and compiler here are stand-in scripts, so each case is exact and
# needs no particular toolchain installed.
#
# Invocation:
#     run_detect_toolchain.sh <source-dir> <cmake>

set -euo pipefail

if [[ $# -ne 2 ]]; then
    echo "usage: $0 <source-dir> <cmake>" >&2
    exit 64
fi

readonly SRC="$1"
readonly CMAKE="$2"
readonly FIXTURE="${SRC}/tests/fixtures/detect_toolchain"

WORK="$(mktemp -d)"
trap 'rm -rf "${WORK}"' EXIT

fail() { echo "FAIL: $*" >&2; exit 1; }
assert_contains() { grep -qE -- "$2" "$1" || { echo "--- $1 ---" >&2; cat "$1" >&2; fail "expected in $1: $2"; }; }
assert_absent()   { ! grep -qE -- "$2" "$1" || { echo "--- $1 ---" >&2; cat "$1" >&2; fail "forbidden in $1: $2"; }; }
# Paths hold regex operators (the + of c++), so results are matched literally.
assert_line()     { grep -qxF -- "-- $2" "$1" || { echo "--- $1 ---" >&2; cat "$1" >&2; fail "expected line in $1: $2"; }; }

# apiary's own builtin headers, and a Clang 21 whose builtin directory is also
# on its include search list, as Apple's and conda's are.
readonly OWN="${WORK}/apiary-prefix/lib/clang/23"
readonly CLANG21="${WORK}/clang21/lib/clang/21"
readonly LIBCXX="${WORK}/clang21/include/c++/v1"
mkdir -p "${OWN}/include" "${CLANG21}/include" "${LIBCXX}"
touch "${OWN}/include/stddef.h" "${CLANG21}/include/stddef.h"

# apiary --print-resource-dir: prints $1, and fails when it holds no headers.
fake_apiary() {
    cat > "$1" <<SH
#!/bin/sh
echo "$2"
[ -f "$2/include/stddef.h" ]
SH
    chmod +x "$1"
}
fake_apiary "${WORK}/apiary-own" "${OWN}"
fake_apiary "${WORK}/apiary-none" "${WORK}/apiary-prefix/lib/clang/none"

# A clang++ (or g++) that answers -print-resource-dir and the -E -v probe,
# whose search list ends in <sysroot>/usr/include when given -isysroot.
fake_cxx() {
    cat > "$1" <<SH
#!/bin/sh
sysroot=""
prev=""
for arg in "\$@"; do
    [ "\$prev" = "-isysroot" ] && sysroot="\$arg"
    prev="\$arg"
done
case "\$*" in
  *-print-resource-dir*) echo "${CLANG21}" ;;
  *) {
       printf '#include <...> search starts here:\n ${LIBCXX}\n ${CLANG21}/include\n'
       [ -n "\$sysroot" ] && printf ' %s/usr/include\n' "\$sysroot"
       printf 'End of search list.\n'
     } >&2 ;;
esac
SH
    chmod +x "$1"
}
mkdir -p "${WORK}/bin"
fake_cxx "${WORK}/bin/clang++"
fake_cxx "${WORK}/bin/g++"

configure() {
    local name="$1"; shift
    # No conda env: the helper would otherwise look for its clang++.
    env -u CONDA_PREFIX "${CMAKE}" -S "${FIXTURE}" -B "${WORK}/build-${name}" \
        -DAPIARY_HELPERS="${SRC}/cmake/ApiaryHelpers.cmake" "$@" >"${WORK}/${name}.log" 2>&1 \
        || { cat "${WORK}/${name}.log" >&2; fail "${name}: configure failed"; }
}

# ── apiary reports its own: used, over the compiler's ─────────────────────────
configure own -DFAKE_APIARY="${WORK}/apiary-own" -DFAKE_CXX="${WORK}/bin/clang++"
assert_line     "${WORK}/own.log" "RESULT resource-dir=${OWN}"
assert_absent   "${WORK}/own.log" "CMake Warning"
# Where the whole list is forwarded, Clang 21's builtin dir becomes apiary's,
# in the same place; elsewhere only the C++ library dirs are.
if [[ "$(uname -s)" == Darwin ]]; then
    assert_line "${WORK}/own.log" "RESULT flags=-resource-dir;${OWN};-isystem;${LIBCXX};-isystem;${OWN}/include"
else
    assert_line "${WORK}/own.log" "RESULT flags=-resource-dir;${OWN};-isystem;${LIBCXX}"
fi

# ── apiary has none: Clang 21's are borrowed, with a warning ─────────────────
configure borrow -DFAKE_APIARY="${WORK}/apiary-none" -DFAKE_CXX="${WORK}/bin/clang++"
assert_line     "${WORK}/borrow.log" "RESULT resource-dir=${CLANG21}"
assert_contains "${WORK}/borrow.log" "no Clang 23 builtin headers were found"
assert_contains "${WORK}/borrow.log" "borrows Clang 21's"
if [[ "$(uname -s)" == Darwin ]]; then
    assert_line "${WORK}/borrow.log" "RESULT flags=-resource-dir;${CLANG21};-isystem;${LIBCXX};-isystem;${CLANG21}/include"
else
    assert_line "${WORK}/borrow.log" "RESULT flags=-resource-dir;${CLANG21};-isystem;${LIBCXX}"
fi

# ── macOS: the probe uses the SDK the project builds against ────────────────
if [[ "$(uname -s)" == Darwin ]]; then
    mkdir -p "${WORK}/sdk/usr/include"
    configure sysroot -DFAKE_APIARY="${WORK}/apiary-own" -DFAKE_CXX="${WORK}/bin/clang++" \
        -DCMAKE_OSX_SYSROOT="${WORK}/sdk"
    assert_line "${WORK}/sysroot.log" \
        "RESULT flags=-resource-dir;${OWN};-isystem;${LIBCXX};-isystem;${OWN}/include;-isystem;${WORK}/sdk/usr/include"
fi

# ── Include dirs the project compiles with come from its flags ──────────────
# Not from $CONDA_PREFIX, the environment active in the shell, which need not
# be the project's: a conda compiler puts its own in CMAKE_CXX_FLAGS.
mkdir -p "${WORK}/shell-env/include" "${WORK}/flags-a" "${WORK}/flags-b"
CONDA_PREFIX="${WORK}/shell-env" "${CMAKE}" -S "${FIXTURE}" -B "${WORK}/build-cxxflags" \
    -DAPIARY_HELPERS="${SRC}/cmake/ApiaryHelpers.cmake" \
    -DFAKE_APIARY="${WORK}/apiary-own" -DFAKE_CXX="${WORK}/bin/clang++" \
    "-DCMAKE_CXX_FLAGS=-O2 -isystem ${WORK}/flags-a -I${WORK}/flags-b" >"${WORK}/cxxflags.log" 2>&1 \
    || { cat "${WORK}/cxxflags.log" >&2; fail "cxxflags: configure failed"; }
if [[ "$(uname -s)" == Darwin ]]; then
    tail_flags=";-isystem;${OWN}/include"
else
    tail_flags=""
fi
assert_line "${WORK}/cxxflags.log" \
    "RESULT flags=-resource-dir;${OWN};-isystem;${WORK}/flags-a;-isystem;${WORK}/flags-b;-isystem;${LIBCXX}${tail_flags}"

# ── Neither apiary nor a Clang has any: said so ──────────────────────────────
configure none -DFAKE_APIARY="${WORK}/apiary-none" -DFAKE_CXX="${WORK}/bin/g++"
assert_line     "${WORK}/none.log" "RESULT resource-dir="
assert_contains "${WORK}/none.log" "no Clang builtin headers \(stddef.h, the intrinsics\) were found"

# ── A binding set without MODULE is warned about ────────────────────────────
configure nomodule -DFAKE_APIARY="${WORK}/apiary-own" -DFAKE_CXX="${WORK}/bin/clang++" \
    -DADD_BINDINGS_WITHOUT_MODULE=ON
assert_contains "${WORK}/nomodule.log" "apiary_add_bindings\(probe\): no MODULE"
assert_contains "${WORK}/nomodule.log" "required in Apiary 2.0"

echo "ok: apiary_detect_toolchain picks apiary's own builtin headers, and warns when it cannot"
