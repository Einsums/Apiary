#!/usr/bin/env bash
# ----------------------------------------------------------------------------------------------
# Copyright (c) The Einsums Developers. All rights reserved.
# Licensed under the MIT License. See LICENSE.txt in the project root for license information.
# ----------------------------------------------------------------------------------------------
#
# Assert that every test suite actually registered in a configured build tree.
#
# Most of apiary's suites register only when a dependency is importable -
# sphinx, docutils, pybind11 - and CMake announces the absence with a
# message(STATUS) that scrolls past unread. The failure mode is quiet and
# total: a suite that never registered is indistinguishable, in a green CI run,
# from one that passed. That is not hypothetical; apiary_sphinx_check sat
# un-run in CI for as long as the workflow forgot to install sphinx, so the
# "the rendered reST must actually build" check was never enforced there.
#
# Run after configure, before ctest.
#
# Invocation:
#     assert_tests_registered.sh <build-dir>

set -euo pipefail

if [[ $# -ne 1 ]]; then
    echo "usage: $0 <build-dir>" >&2
    exit 64
fi

readonly BUILD="$1"

# Every suite CI is expected to run. Adding a test to CMakeLists.txt without
# adding it here means CI would not notice if it stopped registering, so keep
# the two in step.
readonly EXPECTED=(
    apiary_phase2_smoke
    apiary_phase3_golden
    apiary_pyi_golden
    apiary_shard
    apiary_example_mathx
    apiary_properties
    apiary_diagnostics
    apiary_doc_markup
    apiary_sphinx_check
    apiary_cpp_site
    apiary_examples
    apiary_doc_lint
    apiary_py_extract
)

listing="$(ctest --test-dir "${BUILD}" -N 2>/dev/null)"

missing=()
for name in "${EXPECTED[@]}"; do
    if ! grep -q "[[:space:]]${name}\$" <<<"${listing}"; then
        missing+=("${name}")
    fi
done

if (( ${#missing[@]} != 0 )); then
    echo "ERROR: these suites did not register, so they would have been silently skipped:" >&2
    for name in "${missing[@]}"; do
        echo "  - ${name}" >&2
    done
    echo >&2
    echo "Each registers only when its dependency is present. Re-read the configure" >&2
    echo "output for the matching 'apiary: ... - skipping' line and install what it names." >&2
    echo >&2
    echo "--- registered tests ---" >&2
    echo "${listing}" >&2
    exit 1
fi

echo "assert_tests_registered: all ${#EXPECTED[@]} suites registered"
