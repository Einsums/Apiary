#!/usr/bin/env bash
# ----------------------------------------------------------------------------------------------
# Copyright (c) The Einsums Developers. All rights reserved.
# Licensed under the MIT License. See LICENSE.txt in the project root for license information.
# ----------------------------------------------------------------------------------------------
#
# .pyi golden-output runner. Drives each fixture under tests/fixtures/
# through apiary with --stub-output and diffs the produced stub
# against a committed .pyi.golden file. Catches drift in the per-
# instantiation type resolution, docstring extraction, property merge,
# and dtype-/template-kwargs-dispatcher emission.
#
# Every generated stub is also parsed as Python before it is compared. A golden
# only says the output has not CHANGED; it cannot say the output was ever
# valid, and a stub that does not parse is worthless to the type checker it
# exists to feed. The two checks answer different questions and a stub needs
# both. (A C++ name that is not a Python identifier - a free `operator*` - is
# exactly how an unparseable stub gets emitted, silently.)
#
# Updating goldens:
#     REGEN=1 run_pyi_golden.sh <tool> <annotations-include-dir>
#
# Invocation:
#     run_pyi_golden.sh <apiary-binary> <annotations-include-dir> [python]

set -euo pipefail

if [[ $# -lt 2 || $# -gt 3 ]]; then
    echo "usage: $0 <apiary-binary> <annotations-include-dir> [python]" >&2
    exit 64
fi

readonly TOOL="$1"
readonly INCLUDE_DIR="$2"
readonly PY="${3:-python3}"
readonly SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly FIXTURE_DIR="${SCRIPT_DIR}/fixtures"
readonly GOLDEN_DIR="${SCRIPT_DIR}/golden"
readonly REGEN="${REGEN:-0}"

# ``module|fixture|golden`` triples.
readonly CASES=(
    "fixture_simple_class|simple_class.hpp|simple_class.pyi.golden"
    "fixture_free_functions|free_functions.hpp|free_functions.pyi.golden"
    "fixture_enums|enums.hpp|enums.pyi.golden"
    "fixture_templated_class|templated_class.hpp|templated_class.pyi.golden"
    "fixture_templated_function_bools|templated_function_bools.hpp|templated_function_bools.pyi.golden"
    "fixture_namespace_module|namespace_module.hpp|namespace_module.pyi.golden"
    "fixture_dtype_dispatcher|dtype_dispatcher.hpp|dtype_dispatcher.pyi.golden"
    "fixture_member_template|member_template.hpp|member_template.pyi.golden"
    "fixture_enum_nttp|enum_nttp.hpp|enum_nttp.pyi.golden"
    "fixture_qualifiers|qualifiers.hpp|qualifiers.pyi.golden"
    "fixture_default_args|default_args.hpp|default_args.pyi.golden"
    "fixture_signatures_modern|signatures_modern.hpp|signatures_modern.pyi.golden"
    "fixture_operators|operators.hpp|operators.pyi.golden"
    "fixture_std_types|std_types.hpp|std_types.pyi.golden"
    "fixture_cross_module|cross_module.hpp|cross_module.pyi.golden"
    "fixture_docstrings|docstrings.hpp|docstrings.pyi.golden"
)

# Run the tool with --stub-output to a temp file and emit the stub on
# stdout. The .cpp output is discarded — that's covered by run_golden.sh.
run_tool() {
    local module="$1" fixture="$2"
    local tmp_cpp tmp_pyi
    tmp_cpp="$(mktemp)"
    tmp_pyi="$(mktemp)"
    "${TOOL}" --module "${module}" \
              --output "${tmp_cpp}" \
              --stub-output "${tmp_pyi}" \
              "${FIXTURE_DIR}/${fixture}" \
              -- -std=c++20 -nostdinc++ "-I${INCLUDE_DIR}" 2>/dev/null || true
    cat "${tmp_pyi}"
    rm -f "${tmp_cpp}" "${tmp_pyi}"
}

tmp_actual="$(mktemp)"
tmp_diff="$(mktemp)"
trap 'rm -f "${tmp_actual}" "${tmp_diff}"' EXIT

# ast.parse needs nothing but a stdlib interpreter. If there is none, say so
# rather than reporting a pass that skipped half of what this script checks.
can_parse=1
if ! "${PY}" -c 'import ast' >/dev/null 2>&1; then
    can_parse=0
    echo "pyi_golden: SKIPPED the stub-parses-as-Python check - no usable '${PY}'" >&2
fi

failures=0
for case in "${CASES[@]}"; do
    IFS='|' read -r module fixture golden <<<"${case}"
    run_tool "${module}" "${fixture}" > "${tmp_actual}"
    # Validate BEFORE the golden comparison, and before REGEN gets a chance to
    # bless it: an invalid stub must never become a committed golden.
    if [[ "${can_parse}" == "1" ]]; then
        if ! "${PY}" -c 'import ast,sys; ast.parse(open(sys.argv[1]).read())' "${tmp_actual}" 2> "${tmp_diff}"; then
            echo "FAIL ${golden}: generated stub is not valid Python" >&2
            cat "${tmp_diff}" >&2
            failures=$((failures + 1))
            continue
        fi
    fi

    golden_path="${GOLDEN_DIR}/${golden}"

    if [[ "${REGEN}" == "1" ]]; then
        cp "${tmp_actual}" "${golden_path}"
        echo "REGEN ${golden}"
        continue
    fi

    if [[ ! -f "${golden_path}" ]]; then
        echo "FAIL ${golden}: no committed golden file (run with REGEN=1 to create)" >&2
        failures=$((failures + 1))
        continue
    fi

    if ! diff -u "${golden_path}" "${tmp_actual}" > "${tmp_diff}" 2>&1; then
        echo "FAIL ${golden}: stub output drifted from committed golden" >&2
        cat "${tmp_diff}" >&2
        failures=$((failures + 1))
    fi
done

if (( failures != 0 )); then
    echo "${failures} .pyi golden test(s) failed" >&2
    exit 1
fi

echo "OK: all .pyi golden tests pass"
