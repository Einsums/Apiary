#!/usr/bin/env bash
# ----------------------------------------------------------------------------------------------
# Copyright (c) The Einsums Developers. All rights reserved.
# Licensed under the MIT License. See LICENSE.txt in the project root for license information.
# ----------------------------------------------------------------------------------------------
#
# Sharded-emission test. Drives a heavily-instantiated fixture through apiary
# with --max-defs-per-tu and --num-tu and checks the structural + correctness
# guarantees of the split:
#
#   1. --plan and the real emit agree on the shard file list.
#   2. A small budget actually produces more than one shard.
#   3. Each shard stays within the budget (modulo one oversized unit, which
#      can't be split below instantiation granularity).
#   4. Shard 0 carries the dispatcher, which calls every shard function once.
#   5. The bindings emitted across the shards are exactly those of the single
#      TU — no binding dropped or duplicated (compared as the multiset of
#      quoted Python names, ignoring def_submodule preambles).
#   7. --num-tu emits exactly N shards, at the paths derivable from N alone,
#      with the same no-binding-lost guarantee; N=1 falls back to one ordinary
#      TU; an N past the unit count still yields N files; and asking for both
#      split modes at once is refused.
#
# Invocation:
#     run_shard.sh <apiary-binary> <annotations-include-dir>

set -euo pipefail

if [[ $# -ne 2 ]]; then
    echo "usage: $0 <apiary-binary> <annotations-include-dir>" >&2
    exit 64
fi

readonly TOOL="$1"
readonly INCLUDE_DIR="$2"
readonly SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly FIXTURE="${SCRIPT_DIR}/fixtures/templated_class.hpp"
readonly REG="apiary_register_shardtest"
readonly MAX_DEFS=8

workdir="$(mktemp -d)"
trap 'rm -rf "${workdir}"' EXIT
readonly BASE="${workdir}/out_pybind.cpp"

compile_flags=(-std=c++20 -nostdinc++ "-I${INCLUDE_DIR}")

fail() { echo "FAIL: $*" >&2; exit 1; }

# --- 1. plan vs emit agree on the file list -------------------------------
# (bash 3.2 on macOS has no mapfile — read line-by-line into arrays.)
planned=()
while IFS= read -r line; do
    [[ -n "${line}" ]] && planned+=("${line}")
done < <("${TOOL}" --plan --max-defs-per-tu "${MAX_DEFS}" \
    --register-function "${REG}" --output "${BASE}" \
    "${FIXTURE}" -- "${compile_flags[@]}" 2>/dev/null)

"${TOOL}" --max-defs-per-tu "${MAX_DEFS}" \
    --register-function "${REG}" --output "${BASE}" \
    "${FIXTURE}" -- "${compile_flags[@]}" 2>/dev/null

emitted=()
while IFS= read -r line; do
    [[ -n "${line}" ]] && emitted+=("${line}")
done < <(ls "${workdir}"/out_pybind.shard*.cpp 2>/dev/null | sort -V)

(( ${#planned[@]} > 1 )) || fail "expected >1 shard at max-defs-per-tu=${MAX_DEFS}, planned ${#planned[@]}"
(( ${#planned[@]} == ${#emitted[@]} )) || fail "plan listed ${#planned[@]} files, emit wrote ${#emitted[@]}"
# --plan prints each path as the *binary* sees it, which under Git Bash is the
# native C:\... form, while the ``ls`` above yields the shell's POSIX view of
# the very same files. Reduce both to a basename so the existence check is
# comparing like with like on every platform.
for f in "${planned[@]}"; do
    name="$(basename "${f//\\//}")"
    [[ -f "${workdir}/${name}" ]] || fail "planned shard not emitted: ${f}"
done
echo "ok: plan and emit agree on ${#planned[@]} shard(s)"

# --- 2/4. dispatcher in shard 0 calls every shard once --------------------
readonly SHARD0="${workdir}/out_pybind.shard0.cpp"
grep -qE "void[[:space:]]+${REG}[[:space:]]*\(" "${SHARD0}" \
    || fail "dispatcher void ${REG}(...) not found in shard 0"
for ((k = 0; k < ${#emitted[@]}; k++)); do
    grep -qE "void[[:space:]]+${REG}__shard${k}[[:space:]]*\(" "${workdir}/out_pybind.shard${k}.cpp" \
        || fail "shard ${k} does not define ${REG}__shard${k}"
    calls=$(grep -cE "${REG}__shard${k}\(m\);" "${SHARD0}")
    (( calls == 1 )) || fail "dispatcher calls ${REG}__shard${k} ${calls} times (want 1)"
done
echo "ok: dispatcher calls all ${#emitted[@]} shard functions exactly once"

# --- 3. each shard within budget (allow one oversized unit) ---------------
for f in "${emitted[@]}"; do
    n=$(grep -cE '\.def|\.value\(' "${f}" || true)
    # A shard may exceed the budget only by absorbing a single unit larger
    # than it; with this fixture no single instantiation exceeds MAX_DEFS, so
    # require a hard 2x ceiling as a sanity bound.
    (( n <= 2 * MAX_DEFS )) || fail "shard ${f} has ${n} defs, over 2x budget ${MAX_DEFS}"
done
echo "ok: every shard within the budget ceiling"

# --- 5. no binding dropped or duplicated vs the single TU -----------------
# Quoted Python names (class/method/arg/enum-value), ignoring def_submodule
# preambles which legitimately repeat across shards.
pynames() { grep -v def_submodule "$@" | grep -oE '"[A-Za-z_][A-Za-z0-9_]*"' | sort; }

"${TOOL}" --register-function "${REG}" --output "${workdir}/single_pybind.cpp" \
    "${FIXTURE}" -- "${compile_flags[@]}" 2>/dev/null
pynames "${workdir}/single_pybind.cpp" > "${workdir}/single.names"
pynames "${emitted[@]}" > "${workdir}/shards.names"

if ! diff -u "${workdir}/single.names" "${workdir}/shards.names" > "${workdir}/names.diff"; then
    echo "FAIL: shard bindings differ from single-TU bindings" >&2
    cat "${workdir}/names.diff" >&2
    exit 1
fi
echo "ok: shard bindings match the single-TU bindings exactly"

# --- 6. --report-defs total matches the single-TU def count ---------------
report_total=$("${TOOL}" --report-defs --register-function "${REG}" \
    "${FIXTURE}" -- "${compile_flags[@]}" 2>/dev/null | grep -oE ': [0-9]+ binding' | grep -oE '[0-9]+')
single_total=$(grep -cE '\.def|\.value\(' "${workdir}/single_pybind.cpp" || true)
[[ -n "${report_total}" ]] || fail "--report-defs produced no count"
(( report_total == single_total )) \
    || fail "--report-defs total ${report_total} != single-TU def count ${single_total}"
echo "ok: --report-defs total (${report_total}) matches the single-TU def count"

# --- 7. --num-tu: exactly N shards, at names derivable from N -------------
# The mode a build system uses when apiary is built as part of the same
# build: it declares the outputs itself, so the count must be exactly what
# was asked for, whatever the fixture contains.
readonly NUM_TU=5
fixed_dir="${workdir}/fixed"
mkdir -p "${fixed_dir}"
readonly FIXED_BASE="${fixed_dir}/out_pybind.cpp"

"${TOOL}" --num-tu "${NUM_TU}" --register-function "${REG}" --output "${FIXED_BASE}" \
    "${FIXTURE}" -- "${compile_flags[@]}" 2>/dev/null

fixed=()
while IFS= read -r line; do
    [[ -n "${line}" ]] && fixed+=("${line}")
done < <(ls "${fixed_dir}"/out_pybind.shard*.cpp 2>/dev/null | sort -V)

(( ${#fixed[@]} == NUM_TU )) || fail "--num-tu ${NUM_TU} emitted ${#fixed[@]} shard(s)"
for ((k = 0; k < NUM_TU; k++)); do
    # The whole point of the mode: this name is predictable without running
    # the tool, so check the exact path rather than whatever ls turned up.
    [[ -f "${fixed_dir}/out_pybind.shard${k}.cpp" ]] || fail "--num-tu did not write out_pybind.shard${k}.cpp"
    grep -qE "void[[:space:]]+${REG}__shard${k}[[:space:]]*\(" "${fixed_dir}/out_pybind.shard${k}.cpp" \
        || fail "--num-tu shard ${k} does not define ${REG}__shard${k}"
    calls=$(grep -cE "${REG}__shard${k}\(m\);" "${fixed_dir}/out_pybind.shard0.cpp")
    (( calls == 1 )) || fail "--num-tu dispatcher calls ${REG}__shard${k} ${calls} times (want 1)"
done
echo "ok: --num-tu ${NUM_TU} emitted exactly ${NUM_TU} predictably named shards"

pynames "${fixed[@]}" > "${workdir}/fixed.names"
if ! diff -u "${workdir}/single.names" "${workdir}/fixed.names" > "${workdir}/fixed.diff"; then
    echo "FAIL: --num-tu bindings differ from single-TU bindings" >&2
    cat "${workdir}/fixed.diff" >&2
    exit 1
fi
echo "ok: --num-tu bindings match the single-TU bindings exactly"

# --plan agrees, so a build system can use either route to the same names.
planned_fixed=()
while IFS= read -r line; do
    [[ -n "${line}" ]] && planned_fixed+=("${line}")
done < <("${TOOL}" --plan --num-tu "${NUM_TU}" --register-function "${REG}" \
    --output "${FIXED_BASE}" "${FIXTURE}" -- "${compile_flags[@]}" 2>/dev/null)
(( ${#planned_fixed[@]} == NUM_TU )) || fail "--plan --num-tu listed ${#planned_fixed[@]} file(s), want ${NUM_TU}"
echo "ok: --plan agrees with --num-tu on ${NUM_TU} shard(s)"

# N=1 is the degenerate case: one ordinary TU at the base path, no .shard
# suffix, so a build can dial sharding off without changing its file list
# handling.
one_dir="${workdir}/one"
mkdir -p "${one_dir}"
"${TOOL}" --num-tu 1 --register-function "${REG}" --output "${one_dir}/out_pybind.cpp" \
    "${FIXTURE}" -- "${compile_flags[@]}" 2>/dev/null
[[ -f "${one_dir}/out_pybind.cpp" ]] || fail "--num-tu 1 did not write the single TU"
ls "${one_dir}"/out_pybind.shard*.cpp > /dev/null 2>&1 && fail "--num-tu 1 wrote shard files"
echo "ok: --num-tu 1 emits one ordinary TU"

# More shards than the fixture has units: the count still holds, because the
# build system already committed to the file list.
many_dir="${workdir}/many"
mkdir -p "${many_dir}"
readonly MANY=64
"${TOOL}" --num-tu "${MANY}" --register-function "${REG}" --output "${many_dir}/out_pybind.cpp" \
    "${FIXTURE}" -- "${compile_flags[@]}" 2>/dev/null
many_count=$(ls "${many_dir}"/out_pybind.shard*.cpp 2>/dev/null | wc -l | tr -d ' ')
(( many_count == MANY )) || fail "--num-tu ${MANY} emitted ${many_count} shard(s)"
pynames "${many_dir}"/out_pybind.shard*.cpp > "${workdir}/many.names"
diff -q "${workdir}/single.names" "${workdir}/many.names" > /dev/null \
    || fail "--num-tu ${MANY} lost or duplicated bindings"
echo "ok: --num-tu ${MANY} still emits ${MANY} files with every binding intact"

# The two modes disagree about who owns the shard count, so asking for both
# is a build-configuration error, not something to silently resolve.
if "${TOOL}" --num-tu 4 --max-defs-per-tu 8 --register-function "${REG}" \
    --output "${workdir}/both_pybind.cpp" "${FIXTURE}" -- "${compile_flags[@]}" > /dev/null 2>&1; then
    fail "--num-tu together with --max-defs-per-tu was accepted"
fi
echo "ok: --num-tu with --max-defs-per-tu is rejected"

echo "OK: all sharding tests pass"
