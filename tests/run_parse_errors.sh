#!/usr/bin/env bash
# ----------------------------------------------------------------------------------------------
# Copyright (c) The Einsums Developers. All rights reserved.
# Licensed under the MIT License. See LICENSE.txt in the project root for license information.
# ----------------------------------------------------------------------------------------------
#
# What apiary does when clang cannot parse its input.
#
# Clang recovers from errors, so a failed parse still yields output: missing
# whatever followed a fatal error, and with every type it could not resolve
# read as int. Every mode must refuse that unless --allow-parse-errors asks for
# it, and say so.
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

echo "ok: parse errors fail every mode unless allowed"
