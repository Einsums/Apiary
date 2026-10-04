#!/usr/bin/env bash
# ----------------------------------------------------------------------------------------------
# Copyright (c) The Einsums Developers. All rights reserved.
# Licensed under the MIT License. See LICENSE.txt in the project root for license information.
# ----------------------------------------------------------------------------------------------
#
# apiary_aggregate_stubs.py test. Aggregates three fragments, then aggregates
# again with one of them while the other two still sit in the same directory,
# the state a build tree is in after a module is removed. Asserts that only the
# named fragments are merged, that a submodule's .pyi and a helper package's
# stub go once nothing produces them, and that files the aggregator never wrote
# are left alone.
#
# Invocation:
#     run_aggregate_stubs.sh [python-executable]

set -euo pipefail

readonly PY="${1:-python3}"
readonly SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly AGG="${SCRIPT_DIR}/../scripts/apiary_aggregate_stubs.py"

WORK="$(mktemp -d)"
trap 'rm -rf "${WORK}"' EXIT

fail() { echo "FAIL: $*" >&2; exit 1; }
assert_contains() { grep -qE -- "$2" "$1" || { echo "--- $1 ---" >&2; cat "$1" >&2; fail "expected in $1: $2"; }; }
assert_absent()   { ! grep -qE -- "$2" "$1" || { echo "--- $1 ---" >&2; cat "$1" >&2; fail "forbidden in $1: $2"; }; }

FRAG="${WORK}/gen"
PKG="${WORK}/pkg"
HELPERS="${WORK}/helpers"
MANIFEST="${FRAG}/.stubs.manifest"
mkdir -p "${FRAG}" "${PKG}" "${HELPERS}/tools"

# One fragment per module, in the shape PyiEmitter writes: a header, then
# entities under ``# %%submodule: <name>`` sentinels (empty = top level).
fragment() {
    local name="$1"; shift
    {
        echo "# module: demo"
        echo "from __future__ import annotations"
        echo
        printf '%s\n' "$@"
    } > "${FRAG}/${name}.pyi"
}
fragment demo_a "# %%submodule: " "class A: ..." "# %%submodule: shared" "class SharedA: ..."
fragment demo_b "# %%submodule: " "class B: ..."
fragment demo_c "# %%submodule: solo" "class C: ..."

# Files in the package the aggregator does not own: the package's sources.
echo "from ._core import *" > "${PKG}/__init__.py"
echo "def hand() -> int: ..." > "${PKG}/handwritten.pyi"
# A helper sub-package, which gets a stub of its own.
printf 'def tool() -> int:\n    return 1\n' > "${HELPERS}/tools/__init__.py"
# The package's own __init__.py, which __init__.pyi hides from a type checker
# unless the stub carries what it defines and re-exports.
cat > "${HELPERS}/__init__.py" <<'PY'
import os as _os
from ._core import A
from .tools import tool
from ._impl import _secret
__all__ = ["A", "tool", "version"]
LIMIT = 3
CWD: str = _os.getcwd()
HOME = _os.environ["HOME"]
def version() -> str:
    """The version."""
    return "1"
def _hidden() -> None: ...
def __getattr__(name):
    raise AttributeError(name)
PY

run() { "${PY}" "${AGG}" "$@" --pkg-dir "${PKG}" --manifest "${MANIFEST}" >"${WORK}/out.log" 2>&1 \
    || { cat "${WORK}/out.log" >&2; fail "aggregate_stubs exited non-zero: $*"; }; }

# ── All three modules, the list passed as @file ──────────────────────────────
printf '%s\n' "${FRAG}/demo_a.pyi" "${FRAG}/demo_b.pyi" "${FRAG}/demo_c.pyi" > "${FRAG}/list"
run "@${FRAG}/list" --py-helpers-dir "${HELPERS}"
assert_contains "${PKG}/_core.pyi" "^class A:"
assert_contains "${PKG}/_core.pyi" "^class B:"
assert_contains "${PKG}/shared.pyi" "^class SharedA:"
assert_contains "${PKG}/solo.pyi" "^class C:"
[[ -f "${PKG}/tools/__init__.pyi" ]] || fail "helper package stub tools/__init__.pyi was not written"
# The re-export names the package's own _core, whatever the package is called.
assert_contains "${PKG}/__init__.pyi" "^from \._core import \*"
assert_absent   "${PKG}/__init__.pyi" "einsums"
assert_contains "${PKG}/__init__.pyi" "^from \. import solo as solo$"
assert_contains "${PKG}/__init__.pyi" "^from \. import tools as tools$"
# The package's own __init__.py: its public names, its re-exports marked as
# such, its __all__, and assignments as a stub states them.
assert_contains "${PKG}/__init__.pyi" "^def version\(\) -> str:"
assert_contains "${PKG}/__init__.pyi" "^from \._core import A as A$"
assert_contains "${PKG}/__init__.pyi" "^from \.tools import tool as tool$"
assert_contains "${PKG}/__init__.pyi" "^from \._impl import _secret$"
assert_contains "${PKG}/__init__.pyi" "^import os as _os$"
assert_contains "${PKG}/__init__.pyi" "^__all__ = \['A', 'tool', 'version'\]$"
assert_contains "${PKG}/__init__.pyi" "^LIMIT = 3$"
assert_contains "${PKG}/__init__.pyi" "^CWD: str = \.\.\.$"
assert_contains "${PKG}/__init__.pyi" "^HOME: Any$"
assert_absent   "${PKG}/__init__.pyi" "_hidden|__getattr__|getcwd"
"${PY}" -c 'import ast, sys; ast.parse(open(sys.argv[1]).read())' "${PKG}/__init__.pyi" \
    || fail "__init__.pyi is not valid Python"

# ── Only demo_a; demo_b and demo_c stay on disk, as after a module removal ───
rm -rf "${HELPERS:?}/tools"
run "${FRAG}/demo_a.pyi" --py-helpers-dir "${HELPERS}"
assert_contains "${PKG}/_core.pyi" "^class A:"
assert_absent   "${PKG}/_core.pyi" "^class B:"
assert_contains "${PKG}/shared.pyi" "^class SharedA:"
[[ ! -e "${PKG}/solo.pyi" ]] || fail "solo.pyi survived though no module contributes to it"
[[ ! -e "${PKG}/tools" ]] || fail "the removed helper package's stub directory survived"
assert_absent   "${PKG}/__init__.pyi" "import solo|import tools"
assert_contains "${WORK}/out.log" "removed stale solo\.pyi"
# Never touch what the aggregator did not write.
[[ -f "${PKG}/__init__.py" && -f "${PKG}/handwritten.pyi" ]] || fail "a file the aggregator never wrote was removed"
[[ -f "${FRAG}/demo_b.pyi" && -f "${FRAG}/demo_c.pyi" ]] || fail "the aggregator removed a fragment"

# ── An unchanged rerun rewrites nothing ──────────────────────────────────────
mtimes() { "${PY}" -c 'import pathlib, sys; print(sorted((p.name, p.stat().st_mtime_ns) for p in pathlib.Path(sys.argv[1]).rglob("*")))' "${PKG}"; }
before="$(mtimes)"
sleep 1
run "${FRAG}/demo_a.pyi" --py-helpers-dir "${HELPERS}"
after="$(mtimes)"
[[ "${before}" == "${after}" ]] || fail "an unchanged rerun rewrote stubs"

# ── Bad input fails loudly ───────────────────────────────────────────────────
if "${PY}" "${AGG}" "${FRAG}/missing.pyi" --pkg-dir "${PKG}" >"${WORK}/err.log" 2>&1; then
    fail "a missing fragment was accepted"
fi
assert_contains "${WORK}/err.log" "fragment .*missing\.pyi does not exist"
# An empty --py-helpers-dir would read as ".", the working directory.
if "${PY}" "${AGG}" "${FRAG}/demo_a.pyi" --pkg-dir "${PKG}" --py-helpers-dir "" >"${WORK}/err.log" 2>&1; then
    fail "an empty --py-helpers-dir was accepted"
fi
assert_contains "${WORK}/err.log" "--py-helpers-dir: must not be empty"

echo "ok: aggregate_stubs merges only the named fragments and prunes what it no longer writes"
