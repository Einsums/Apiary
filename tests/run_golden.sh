#!/usr/bin/env bash
# ----------------------------------------------------------------------------------------------
# Copyright (c) The Einsums Developers. All rights reserved.
# Licensed under the MIT License. See LICENSE.txt in the project root for license information.
# ----------------------------------------------------------------------------------------------
#
# Phase-3 golden-output runner. Drives each fixture under tests/fixtures/
# through apiary in emitter mode and diffs the result against a
# committed .golden file. Differences are reported with `diff -u` so the
# test driver shows exactly which lines moved.
#
# Updating goldens:
#     <tool> --module <name> <fixture> -- -std=c++20 -nostdinc++ -I<include>
#         > tests/golden/<name>.cpp.golden
# Or run with REGEN=1 to overwrite goldens in place.
#
# Invocation:
#     run_golden.sh <apiary-binary> <annotations-include-dir>

set -euo pipefail

if [[ $# -ne 2 ]]; then
    echo "usage: $0 <apiary-binary> <annotations-include-dir>" >&2
    exit 64
fi

readonly TOOL="$1"
readonly INCLUDE_DIR="$2"
readonly SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly FIXTURE_DIR="${SCRIPT_DIR}/fixtures"
readonly GOLDEN_DIR="${SCRIPT_DIR}/golden"
readonly REGEN="${REGEN:-0}"

# ``module|fixture|golden`` triples.
readonly CASES=(
    "fixture_simple_class|simple_class.hpp|simple_class.cpp.golden"
    "fixture_free_functions|free_functions.hpp|free_functions.cpp.golden"
    "fixture_enums|enums.hpp|enums.cpp.golden"
    "fixture_templated_class|templated_class.hpp|templated_class.cpp.golden"
    "fixture_templated_function_bools|templated_function_bools.hpp|templated_function_bools.cpp.golden"
    "fixture_namespace_module|namespace_module.hpp|namespace_module.cpp.golden"
    "fixture_dtype_dispatcher|dtype_dispatcher.hpp|dtype_dispatcher.cpp.golden"
    "fixture_member_template|member_template.hpp|member_template.cpp.golden"
    "fixture_enum_nttp|enum_nttp.hpp|enum_nttp.cpp.golden"
)

# Run apiary over one fixture, writing the generated TU to $1.
#
# stderr is captured rather than discarded, and both a non-zero exit and empty
# output are hard failures. Previously this swallowed stderr and ended in
# `|| true`, so a tool that could not run at all (bad include dir, missing
# binary, a parse error in a fixture) produced an empty file and the script
# carried on. Under REGEN=1 that silently overwrote every golden with an empty
# file and then reported "OK: all pass", because it was comparing empty to
# empty. Fail loudly instead.
run_tool() {
    local out="$1" module="$2" fixture="$3"
    local err status
    err="$(mktemp)"

    set +e
    "${TOOL}" --module "${module}" "${FIXTURE_DIR}/${fixture}" \
        -- -std=c++20 -nostdinc++ "-I${INCLUDE_DIR}" > "${out}" 2> "${err}"
    status=$?
    set -e

    if (( status != 0 )); then
        echo "ERROR ${fixture}: apiary exited ${status}" >&2
        cat "${err}" >&2
        rm -f "${err}"
        return 1
    fi

    if [[ ! -s "${out}" ]]; then
        echo "ERROR ${fixture}: apiary exited 0 but produced no output." >&2
        echo "  tool:    ${TOOL}" >&2
        echo "  include: ${INCLUDE_DIR}" >&2
        echo "  (both arguments must be ABSOLUTE paths - this script cds to" >&2
        echo "   its own directory, so relative paths resolve elsewhere.)" >&2
        if [[ -s "${err}" ]]; then
            echo "--- apiary stderr ---" >&2
            cat "${err}" >&2
        fi
        rm -f "${err}"
        return 1
    fi

    rm -f "${err}"
}

tmp_actual="$(mktemp)"
tmp_diff="$(mktemp)"
trap 'rm -f "${tmp_actual}" "${tmp_diff}"' EXIT

# The emitter calls clang::format::getStyle("file", path_hint, "LLVM", ...) to
# locate a `.clang-format`. The tool's default path_hint is "generated.cpp"
# resolved against CWD. ctest's default working dir is the build tree; in
# out-of-source builds (the CI default) the build tree is a sibling of the
# source tree, so the upward search never reaches the project root's
# `.clang-format` and the emitter silently falls back to LLVM style (2-space).
# That produces a stable mismatch against goldens generated under the project
# style (4-space). Run from this script's directory — inside the source tree —
# so the upward search hits the project's `.clang-format` regardless of where
# the build tree lives.
cd "${SCRIPT_DIR}"

failures=0
for case in "${CASES[@]}"; do
    IFS='|' read -r module fixture golden <<<"${case}"
    if ! run_tool "${tmp_actual}" "${module}" "${fixture}"; then
        failures=$((failures + 1))
        continue
    fi
    golden_path="${GOLDEN_DIR}/${golden}"

    if [[ "${REGEN}" == "1" ]]; then
        cp "${tmp_actual}" "${golden_path}"
        echo "REGEN ${golden}"
        continue
    fi

    if ! diff -u "${golden_path}" "${tmp_actual}" > "${tmp_diff}" 2>&1; then
        echo "FAIL ${golden}: emitter output drifted from committed golden" >&2
        cat "${tmp_diff}" >&2
        failures=$((failures + 1))
    fi
done

if (( failures != 0 )); then
    echo "${failures} golden test(s) failed" >&2
    exit 1
fi

if [[ "${REGEN}" == "1" ]]; then
    # Not a pass: nothing was verified. Say so, so a regen is never mistaken
    # for a green run.
    echo "REGEN complete: ${#CASES[@]} golden(s) rewritten. Review 'git diff' before committing."
else
    echo "OK: all Phase-3 golden tests pass"
fi
