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
# overload sets merge, the best-documented duplicate wins, every kind of
# template parameter renders as declared), and, when sphinx-build is
# available, builds the result with ``-W -n`` so a duplicate declaration or
# malformed directive fails.
#
# The pages rendered from geom/Templates.hpp are also diffed against
# tests/golden/cpp_site_declarations.rst.golden. Run with REGEN=1 to rewrite it.
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
readonly DECLS_GOLDEN="${SCRIPT_DIR}/golden/cpp_site_declarations.rst.golden"
readonly REGEN="${REGEN:-0}"

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
gen geom/Templates.hpp "${WORK}/Templates.json"
readonly JSONS=("${WORK}/Shapes.json" "${WORK}/Ops.json" "${WORK}/Templates.json")

SITE="${WORK}/src/geom"
"${PY}" "${SCRIPTS_DIR}/apiary_render_cpp_site.py" --outdir "${SITE}" \
    --module-title geom --index-label geom_api --backlink-label geom_narrative \
    "${JSONS[@]}" 2>/dev/null

# ---- page inventory --------------------------------------------------------
# Pages rendered from Templates.hpp; the golden covers these.
readonly DECL_PAGES=(
    geom.advance geom.Grid geom.make_fixed geom.print geom.rebuild geom.repack geom.scaled geom.sum geom.unfold
    types
)
for page in index geom.Circle geom.Scalar geom.scale enums macros operators "${DECL_PAGES[@]}"; do
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

# ---- template parameters render as declared --------------------------------
# Every kind of template parameter keeps its kind: a non-type parameter its
# type, a pack its ``...``, a template template parameter its parameter list,
# a constrained parameter its concept. The regression was every one of them
# coming out as ``typename <name>``.
assert_grep "template <unsigned int mode, size_t CRank, typename T> Grid<T, 2> unfold(" "${SITE}/geom.unfold.rst"
assert_grep "template <typename... Args> Real sum(" "${SITE}/geom.sum.rst"
assert_grep "template <size_t... Ns> Grid<Real, sizeof...(Ns)> make_fixed()" "${SITE}/geom.make_fixed.rst"
assert_grep "template <template <typename, size_t> typename TT> TT<Real, 2> rebuild(" "${SITE}/geom.rebuild.rst"
assert_grep "template <template <typename Elem, size_t Extent> typename Container> Container<Real, 3> repack(" "${SITE}/geom.repack.rst"
assert_grep "template <Scalar auto Step> Real advance(Real x)" "${SITE}/geom.advance.rst"
assert_grep "template <Scalar S, int Offset = 0> Grid<S, 2> scaled(" "${SITE}/geom.scaled.rst"
assert_grep ".. cpp:class:: template <typename T, size_t Rank = 2> Grid" "${SITE}/geom.Grid.rst"
assert_grep "template <typename F, bool Unroll = false> void fill(" "${SITE}/geom.Grid.rst"
assert_grep "template <size_t NewRank, typename... Dims> Grid<T, NewRank> reshape(" "${SITE}/geom.Grid.rst"
assert_grep ".. cpp:type:: template <typename T, size_t N = 3> Square" "${SITE}/types.rst"
# The parameter clang invents for ``auto const &...values`` is not declared:
# the ``auto`` already makes the template, and declaring it twice is a
# duplicate declaration.
assert_grep "template <typename Sep = char> void print(Sep sep, const auto &... values)" "${SITE}/geom.print.rst"

actual_decls="${WORK}/declarations.rst"
for page in "${DECL_PAGES[@]}"; do
    printf '==> %s.rst <==\n' "${page}"
    cat "${SITE}/${page}.rst"
done > "${actual_decls}"
if [[ "${REGEN}" == "1" ]]; then
    cp "${actual_decls}" "${DECLS_GOLDEN}"
    echo "REGEN $(basename "${DECLS_GOLDEN}")"
elif ! diff -u "${DECLS_GOLDEN}" "${actual_decls}"; then
    fail "declaration pages drifted from $(basename "${DECLS_GOLDEN}")"
fi

# ---- rerunning is a no-op on disk ------------------------------------------
# Generation is deterministic, so a second run with the same input must not
# touch a single page. This is not cosmetic: the mtime is what Sphinx keys its
# incremental rebuild off, so a renderer that rewrites unconditionally forces a
# full re-read of the API reference on every build. Easy to regress, invisible
# without a test.
#
# ``stat`` is the wrong tool for this: BSD spells the mtime format ``-f %m``
# while GNU reads ``-f`` as --file-system and ignores the format entirely, so
# a BSD-first fallback chain reports free block and inode counts on Linux.
# Those drift with unrelated disk activity, which under a parallel ctest turns
# the comparison below into a coin flip. Ask Python for the mtime instead; it
# is already a hard dependency of this script.
snapshot() {
    "${PY}" -c 'import os, sys
print("\n".join(sorted("%s %d" % (os.path.basename(p), os.stat(p).st_mtime_ns) for p in sys.argv[1:])))' \
        "${SITE}"/*.rst
}
before="$(snapshot)"
sleep 1.1   # coarser-than-1s mtime granularity would mask a rewrite
"${PY}" "${SCRIPTS_DIR}/apiary_render_cpp_site.py" --outdir "${SITE}" \
    --module-title geom --index-label geom_api --backlink-label geom_narrative \
    "${JSONS[@]}" 2>/dev/null
after="$(snapshot)"
[[ "${before}" == "${after}" ]] || fail "re-render rewrote unchanged pages:
$(diff <(echo "${before}") <(echo "${after}") || true)"

# ...but pruning still has to work, which is why the pages are not simply
# cleared up front. An entity that disappears must take its page with it.
touch "${SITE}/geom.Ghost.rst"
"${PY}" "${SCRIPTS_DIR}/apiary_render_cpp_site.py" --outdir "${SITE}" \
    --module-title geom --index-label geom_api --backlink-label geom_narrative \
    "${JSONS[@]}" 2>/dev/null
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

if [[ "${REGEN}" == "1" ]]; then
    # Not a pass: the golden was rewritten, not verified. Say so, so a regen
    # is never mistaken for a green run.
    echo "REGEN complete: 1 golden rewritten. Review 'git diff' before committing."
    exit 0
fi

echo "PASS: run_cpp_site"
