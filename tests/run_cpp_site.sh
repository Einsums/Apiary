#!/usr/bin/env bash
# ----------------------------------------------------------------------------------------------
# Copyright (c) The Einsums Developers. All rights reserved.
# Licensed under the MIT License. See LICENSE.txt in the project root for license information.
# ----------------------------------------------------------------------------------------------
#
# Per-entity C++ site renderer check. Drives the tests/fixtures/cpp_site
# headers through ``apiary --emit-cpp-docs-json`` and
# ``apiary_render_cpp_site.py``, asserts the per-entity page set and the
# soundness rules (const/non-const overloads stay distinct, cross-header
# overload sets merge, the best-documented duplicate wins), and, when
# sphinx-build is available, builds the result with ``-W -n`` so a duplicate
# declaration or malformed directive fails.
#
# Invocation:
#     run_cpp_site.sh <apiary-binary> <apiary-include-dir> <python> [sphinx-build]

set -euo pipefail

if [[ $# -lt 3 || $# -gt 4 ]]; then
    echo "usage: $0 <apiary-binary> <apiary-include-dir> <python> [sphinx-build]" >&2
    exit 64
fi

readonly TOOL="$1"
readonly INCLUDE_DIR="$2"
readonly PY="$3"
readonly SPHINX="${4:-}"
readonly SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly REPO_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
readonly SCRIPTS_DIR="${REPO_DIR}/scripts"
readonly FIX="${SCRIPT_DIR}/fixtures/cpp_site"

WORK="$(mktemp -d)"
trap 'rm -rf "${WORK}"' EXIT

fail() { echo "FAIL: $*" >&2; exit 1; }

assert_file() { [[ -f "$1" ]] || fail "missing expected page: $1"; }

assert_grep() {
    local pattern="$1" file="$2"
    grep -qF -- "${pattern}" "${file}" || fail "pattern not found in ${file}: ${pattern}"
}

# The tool may exit non-zero for missing system headers under -nostdinc++;
# the emitted JSON is still complete for the fixture's self-contained types.
gen() {
    local rel="$1" out="$2"
    "${TOOL}" --emit-cpp-docs-json --module geom --source-include "${rel}" \
        "${FIX}/include/${rel}" -- -std=c++20 -nostdinc++ \
        "-I${FIX}/include" "-I${INCLUDE_DIR}" 2>/dev/null > "${out}" || true
    [[ -s "${out}" ]] || fail "apiary emitted no JSON for ${rel}"
}

gen geom/Shapes.hpp "${WORK}/Shapes.json"
gen geom/Ops.hpp "${WORK}/Ops.json"

SITE="${WORK}/src/geom"
"${PY}" "${SCRIPTS_DIR}/apiary_render_cpp_site.py" --outdir "${SITE}" \
    --module-title geom --index-label geom_api --backlink-label geom_narrative \
    "${WORK}/Shapes.json" "${WORK}/Ops.json" 2>/dev/null

# ---- page inventory --------------------------------------------------------
for page in index geom.Circle geom.Scalar geom.scale enums types macros operators; do
    assert_file "${SITE}/${page}.rst"
done

# ---- soundness rules -------------------------------------------------------
# const/non-const overload pair renders two distinct signatures.
assert_grep "Real radius() const" "${SITE}/geom.Circle.rst"
assert_grep "void radius(Real r)" "${SITE}/geom.Circle.rst"
# Ref-qualified overloads stay distinct for the same reason: without the
# qualifier all three collapse onto ``center()`` and Sphinx reports duplicates.
assert_grep "Real & center() &" "${SITE}/geom.Circle.rst"
assert_grep "Real && center() &&" "${SITE}/geom.Circle.rst"
assert_grep "const Real & center() const &" "${SITE}/geom.Circle.rst"
# The overload set merged across both headers and says so.
assert_grep "geom/Ops.hpp" "${SITE}/geom.scale.rst"
assert_grep "geom/Shapes.hpp" "${SITE}/geom.scale.rst"
assert_grep "Circle scale(const Circle &c, int factor)" "${SITE}/geom.scale.rst"
# The definition's @param docs beat the forward declaration's brief.
assert_grep ":param factor: The scale factor." "${SITE}/geom.scale.rst"
# The identical-signature duplicate collapsed to ONE directive.
[[ "$(grep -cF "Circle scale(const Circle &c, Real factor)" "${SITE}/geom.scale.rst")" == 1 ]] \
    || fail "duplicate scale(Real) declaration survived the merge"
# Operators land on the catch-all page, macros in the C domain.
assert_grep "operator==" "${SITE}/operators.rst"
assert_grep ".. c:macro:: GEOM_CLAMP(x, lo, hi)" "${SITE}/macros.rst"
# The index links every entity but declares none.
assert_grep ":cpp:any:\`~geom::Circle\`" "${SITE}/index.rst"
assert_grep ":ref:\`narrative documentation <geom_narrative>\`" "${SITE}/index.rst"
if grep -qE "^\.\. cpp:(class|function|enum|type|concept)::" "${SITE}/index.rst"; then
    fail "index.rst declares an entity; it must only link"
fi

# ---- rerunning is a no-op on disk ------------------------------------------
# Generation is deterministic, so a second run with the same input must not
# touch a single page. This is not cosmetic: the mtime is what Sphinx keys its
# incremental rebuild off, so a renderer that rewrites unconditionally forces a
# full re-read of the API reference on every build. Easy to regress, invisible
# without a test.
snapshot() { for f in "${SITE}"/*.rst; do echo "$(basename "$f") $(stat -f %m "$f" 2>/dev/null || stat -c %Y "$f")"; done | sort; }
before="$(snapshot)"
sleep 1.1   # coarser-than-1s mtime granularity would mask a rewrite
"${PY}" "${SCRIPTS_DIR}/apiary_render_cpp_site.py" --outdir "${SITE}" \
    --module-title geom --index-label geom_api --backlink-label geom_narrative \
    "${WORK}/Shapes.json" "${WORK}/Ops.json" 2>/dev/null
after="$(snapshot)"
[[ "${before}" == "${after}" ]] || fail "re-render rewrote unchanged pages:
$(diff <(echo "${before}") <(echo "${after}") || true)"

# ...but pruning still has to work, which is why the pages are not simply
# cleared up front. An entity that disappears must take its page with it.
touch "${SITE}/geom.Ghost.rst"
"${PY}" "${SCRIPTS_DIR}/apiary_render_cpp_site.py" --outdir "${SITE}" \
    --module-title geom --index-label geom_api --backlink-label geom_narrative \
    "${WORK}/Shapes.json" "${WORK}/Ops.json" 2>/dev/null
[[ ! -f "${SITE}/geom.Ghost.rst" ]] || fail "stale page survived the re-render"
assert_file "${SITE}/geom.Circle.rst"

# ---- the pages build under -W -n ------------------------------------------
if [[ -n "${SPHINX}" ]]; then
    cat > "${WORK}/src/conf.py" <<'CONF'
project = "cpp-site-check"
extensions = []
primary_domain = "cpp"
highlight_language = "cpp"
root_doc = "index"
html_theme = "alabaster"
# Template-parameter names are never cross-reference targets (the docs build
# collects them from gen_cpp_docs' template_params.txt; the fixture has one).
nitpick_ignore = [("cpp:identifier", "T")]
CONF
    cat > "${WORK}/src/index.rst" <<'INDEX'
cpp-site-check
==============

.. _geom_narrative:

Narrative stub for the backlink target.

.. toctree::
   :maxdepth: 1

   geom/index
INDEX
    "${SPHINX}" -W -n -q -b html "${WORK}/src" "${WORK}/html"
    [[ -f "${WORK}/html/geom/index.html" ]] || fail "sphinx produced no geom/index.html"
fi

echo "PASS: run_cpp_site"
