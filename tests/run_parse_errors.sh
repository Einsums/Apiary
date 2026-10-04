#!/usr/bin/env bash
# ----------------------------------------------------------------------------------------------
# Copyright (c) The Einsums Developers. All rights reserved.
# Licensed under the MIT License. See LICENSE.txt in the project root for license information.
# ----------------------------------------------------------------------------------------------
#
# What apiary does when clang cannot parse its input, and where it finds
# Clang's builtin headers.
#
# Clang recovers from errors, so a failed parse still yields output: missing
# whatever followed a fatal error, and with every type it could not resolve
# read as int. Every mode must refuse that unless --allow-parse-errors asks for
# it, and say so. A parse that fails for want of the builtin headers must say
# which headers and how to get them.
#
# Invocation:
#     run_parse_errors.sh <apiary-binary> <apiary-include-dir>

set -euo pipefail

if [[ $# -ne 2 ]]; then
    echo "usage: $0 <apiary-binary> <apiary-include-dir>" >&2
    exit 64
fi

readonly TOOL="$1"
readonly INCLUDE_DIR="$2"

WORK="$(mktemp -d)"
trap 'rm -rf "${WORK}"' EXIT

fail() { echo "FAIL: $*" >&2; exit 1; }
assert_contains() { grep -qE -- "$2" "$1" || { echo "--- $1 ---" >&2; cat "$1" >&2; fail "expected in $1: $2"; }; }

# Declares something on both sides of the include that cannot be found.
cat > "${WORK}/broken.hpp" <<'EOF'
#include <apiary/Annotations.hpp>

/// Before the error.
APIARY_EXPOSE inline int before() { return 1; }

#include <no_such_header.h>

/// After the error.
APIARY_EXPOSE inline int after() { return 2; }
EOF

# Needs only a builtin header. size_t is the tell: unresolved, it reads as int.
cat > "${WORK}/builtin.hpp" <<'EOF'
#include <stddef.h>

/// The size of a probe.
size_t probe_size();
EOF

flags=(-- -std=c++17 "-I${INCLUDE_DIR}")

# ── Every mode refuses a failed parse, and writes nothing ────────────────────
for mode in --emit-cpp-docs-json --emit-docs-json --dump-ir ""; do
    label="${mode:-binding}"
    out="${WORK}/out-${label#--}"
    rm -f "${out}"
    if [[ -n "${mode}" ]]; then
        set +e; "${TOOL}" "${mode}" --module t "${WORK}/broken.hpp" "${flags[@]}" >"${out}" 2>"${WORK}/err"; rc=$?; set -e
        [[ ! -s "${out}" ]] || fail "${label}: wrote output after a parse error"
    else
        set +e; "${TOOL}" --module t --output "${out}" --stub-output "${out}.pyi" "${WORK}/broken.hpp" "${flags[@]}" 2>"${WORK}/err"; rc=$?; set -e
        [[ ! -e "${out}" && ! -e "${out}.pyi" ]] || fail "${label}: wrote output after a parse error"
    fi
    [[ ${rc} -eq 1 ]] || fail "${label}: exit ${rc} after a parse error, expected 1"
    assert_contains "${WORK}/err" "no_such_header\.h' file not found"
    assert_contains "${WORK}/err" "apiary: clang reported errors parsing the input; writing nothing"
done

# ── --allow-parse-errors writes what clang made of it ────────────────────────
"${TOOL}" --emit-cpp-docs-json --allow-parse-errors --module t "${WORK}/broken.hpp" "${flags[@]}" \
    >"${WORK}/allowed.json" 2>"${WORK}/err" || fail "--allow-parse-errors: exit $? after a parse error, expected 0"
assert_contains "${WORK}/allowed.json" '"before"'
assert_contains "${WORK}/err" "writing the output anyway \(--allow-parse-errors\)"

# ── Builtin headers: found with no -resource-dir, named when missing ─────────
# -nostdlibinc drops the system include paths (an SDK, MSVC, glibc) but keeps
# the builtin headers, so <stddef.h> can only come from apiary's own.
"${TOOL}" --emit-cpp-docs-json --module t "${WORK}/builtin.hpp" -- -std=c++17 -nostdlibinc \
    >"${WORK}/builtin.json" 2>"${WORK}/err" || { cat "${WORK}/err" >&2; fail "builtin headers not found by default"; }
assert_contains "${WORK}/builtin.json" '"return_type": "size_t"'

resource_dir="$("${TOOL}" --print-resource-dir)" || fail "--print-resource-dir: exit $?"
# A native Windows apiary prints C:\... ; Git Bash tests files by its own spelling.
if command -v cygpath >/dev/null 2>&1; then resource_dir="$(cygpath -u "${resource_dir}")"; fi
[[ -f "${resource_dir}/include/stddef.h" ]] || fail "--print-resource-dir printed '${resource_dir}', which has no builtin headers"

set +e
"${TOOL}" --emit-cpp-docs-json --module t "${WORK}/builtin.hpp" -- -std=c++17 -nostdlibinc \
    -resource-dir "${WORK}/no-headers" >/dev/null 2>"${WORK}/err"
rc=$?
set -e
[[ ${rc} -eq 1 ]] || fail "missing builtin headers: exit ${rc}, expected 1"
# Only the tail: under Git Bash, MSYS rewrites the /tmp path apiary is passed.
assert_contains "${WORK}/err" "builtin headers .* are not in '.*no-headers/include'"
assert_contains "${WORK}/err" "install clang-[0-9]+ beside apiary"

echo "ok: parse errors fail every mode unless allowed; builtin headers are found, or named when missing"
