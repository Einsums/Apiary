#!/usr/bin/env bash
# ----------------------------------------------------------------------------------------------
# Copyright (c) The Einsums Developers. All rights reserved.
# Licensed under the MIT License. See LICENSE.txt in the project root for license information.
# ----------------------------------------------------------------------------------------------
#
# Property suite. Runs tests/properties.py over EVERY fixture in tests/fixtures,
# not a hand-listed subset - a property that only holds for the cases someone
# remembered to enumerate is not a property.
#
# What it checks and why is documented in properties.py. The short version: a
# golden pins one input to one output and says nothing about the relationships
# between outputs, which is where the binding and the stub drifting apart, or
# emission not being reproducible, would hide.
#
# There is no golden here, deliberately. These assertions are about invariants,
# and an invariant that has to be re-blessed with REGEN=1 is not one.
#
# Invocation:
#     run_properties.sh <apiary-binary> <annotations-include-dir> <python>

set -euo pipefail

if [[ $# -ne 3 ]]; then
    echo "usage: $0 <apiary-binary> <annotations-include-dir> <python>" >&2
    exit 64
fi

readonly TOOL="$1"
readonly INCLUDE_DIR="$2"
readonly PY="$3"
readonly SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# New fixtures are picked up automatically; that is the point of globbing here
# rather than keeping a CASES list like the golden runners do. Those need a
# per-case golden name, this does not.
shopt -s nullglob
fixtures=("${SCRIPT_DIR}"/fixtures/*.hpp)
shopt -u nullglob

if (( ${#fixtures[@]} == 0 )); then
    echo "FAIL: no fixtures found under ${SCRIPT_DIR}/fixtures" >&2
    exit 1
fi

exec "${PY}" "${SCRIPT_DIR}/properties.py" "${TOOL}" "${INCLUDE_DIR}" "${fixtures[@]}"
