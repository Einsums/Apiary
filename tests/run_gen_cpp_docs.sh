#!/usr/bin/env bash
# ----------------------------------------------------------------------------------------------
# Copyright (c) The Einsums Developers. All rights reserved.
# Licensed under the MIT License. See LICENSE.txt in the project root for license information.
# ----------------------------------------------------------------------------------------------
#
# C++ reference generator check. Drives apiary_gen_cpp_docs.py over a small
# project laid out the way it expects (libs/<lib>/<module>/include plus a
# build.ninja carrying a Tensor codegen command to take flags from) and checks
# how it parses:
#
# - A module whose headers compile together is parsed ONCE, through a
#   generated umbrella, and still documents every header, including a
#   documented macro in a header that is not the file apiary was given.
# - A module whose headers do not (one only compiles under a macro another
#   header defines) falls back to one parse per header, and loses nothing.
# - --layout header splits a module's parse back into per-header pages.
# - Output does not depend on --jobs.
# - JSON a previous run wrote for a header that is gone is removed.
#
# Invocation:
#     run_gen_cpp_docs.sh <apiary-binary> <python>

set -euo pipefail

if [[ $# -ne 2 ]]; then
    echo "usage: $0 <apiary-binary> <python>" >&2
    exit 64
fi

readonly TOOL="$1"
readonly PY="$2"
readonly SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly GEN="${SCRIPT_DIR}/../scripts/apiary_gen_cpp_docs.py"

WORK="$(mktemp -d)"
trap 'rm -rf "${WORK}"' EXIT

fail() { echo "FAIL: $*" >&2; exit 1; }
assert_grep() { grep -qF -- "$1" "$2" || fail "pattern not found in $2: $1"; }
assert_no_grep() { ! grep -qF -- "$1" "$2" || fail "unexpected pattern in $2: $1"; }

# ---- the project -----------------------------------------------------------
SRC="${WORK}/src"
ALPHA="${SRC}/libs/Demo/Alpha/include/Demo/Alpha"
BETA="${SRC}/libs/Demo/Beta/include/Demo/Beta"
mkdir -p "${ALPHA}" "${BETA}" "${WORK}/build"

# One.hpp only forward-declares Widget and Mode; Two.hpp defines and documents
# them. Parsed as one module, the forward declarations come first, and a doc
# comment is found through any declaration, so they must not stand in for the
# definitions: the pages list Widget's members and Mode's enumerators, from
# Two.hpp.
cat > "${ALPHA}/One.hpp" <<'HPP'
#pragma once
namespace demo {
class Widget;
enum class Mode : int;

/// The first function.
/// @tparam T The value type.
template <typename T>
T one(T x);

/// Use a widget.
void use(Widget const &w, Mode m);
} // namespace demo
HPP

cat > "${ALPHA}/Two.hpp" <<'HPP'
#pragma once
#include <Demo/Alpha/One.hpp>

/// Twice a value.
#define DEMO_TWICE(x) ((x) + (x))

namespace demo {
/// The second function.
int two(int x);

/// A widget.
class Widget {
  public:
    /// Spin the widget.
    void spin();
};

/// How a widget runs.
enum class Mode : int {
    /// Slowly.
    Slow,
    /// Quickly.
    Fast,
};
} // namespace demo
HPP

cat > "${BETA}/Good.hpp" <<'HPP'
#pragma once
namespace demo {
/// A function every configuration has.
int good();
} // namespace demo
HPP

# Like a kernel body included once per architecture: it refuses to compile
# unless its includer names the namespace, so it cannot join an umbrella.
cat > "${BETA}/Body.hpp" <<'HPP'
#ifndef DEMO_BODY_NS
#    error "Define DEMO_BODY_NS before including Body.hpp"
#endif
HPP

# The generator takes its compile flags from a codegen command in build.ninja.
cat > "${WORK}/build/build.ninja" <<NINJA
build x: CUSTOM_COMMAND
  COMMAND = cmake -DAPIARY_COMMAND=apiary;--register-function;apiary_register_Tensor;x.hpp;--;-std=c++20;-nostdinc++ -P run.cmake
NINJA

gen() {
    local out="$1"
    shift
    "${PY}" "${GEN}" --source-dir "${SRC}" --build-dir "${WORK}/build" --tool "${TOOL}" \
        --out-dir "${out}" --modules Demo/Alpha Demo/Beta "$@" 2> "${out}.log" \
        || { cat "${out}.log" >&2; fail "generator exited non-zero"; }
}

# ---- entity layout ---------------------------------------------------------
gen "${WORK}/one" --layout entity --jobs 1
SITE="${WORK}/one/rst/Demo"

# Alpha parsed whole; Beta could not be, and says so.
assert_no_grep "Demo/Alpha: generated pages (" "${WORK}/one.log"
assert_grep "Demo/Beta: generated pages (headers do not parse together; parsed one at a time)" "${WORK}/one.log"
[[ -f "${WORK}/one/Demo_Alpha.module.json" ]] || fail "Alpha was not parsed as one module"
[[ -f "${WORK}/one/umbrellas/Demo_Alpha.hpp" ]] || fail "no umbrella written for Alpha"

# Every header of both modules is documented.
assert_grep "T one(T x)" "${SITE}/Alpha/demo.one.rst"
assert_grep "int two(int x)" "${SITE}/Alpha/demo.two.rst"
assert_grep "int good()" "${SITE}/Beta/demo.good.rst"
# The macro lives in Two.hpp, not in the umbrella apiary was given.
assert_grep ".. c:macro:: DEMO_TWICE(x)" "${SITE}/Alpha/macros.rst"
assert_grep "Demo/Alpha/Two.hpp" "${SITE}/Alpha/macros.rst"
assert_grep "T" "${WORK}/one/template_params.txt"
# A forward declaration does not replace the documented definition.
assert_grep "void spin()" "${SITE}/Alpha/demo.Widget.rst"
assert_grep "Demo/Alpha/Two.hpp" "${SITE}/Alpha/demo.Widget.rst"
assert_no_grep "Demo/Alpha/One.hpp" "${SITE}/Alpha/demo.Widget.rst"
assert_grep "cpp:enumerator:: Fast" "${SITE}/Alpha/enums.rst"

# The same output however many modules run at once.
gen "${WORK}/many" --layout entity --jobs 4
diff -r "${WORK}/one/rst" "${WORK}/many/rst" || fail "--jobs 4 rendered different pages than --jobs 1"
diff "${WORK}/one/template_params.txt" "${WORK}/many/template_params.txt" \
    || fail "--jobs 4 collected different template parameters"

# A per-header JSON left by an older run is removed, and a rerun is otherwise
# stable.
echo '{}' > "${WORK}/one/Demo_Alpha_Gone_hpp.json"
gen "${WORK}/one" --layout entity --jobs 1
[[ ! -e "${WORK}/one/Demo_Alpha_Gone_hpp.json" ]] || fail "stale per-header JSON survived a rerun"
diff -r "${WORK}/one/rst" "${WORK}/many/rst" || fail "a rerun changed the pages"

# ---- header layout ---------------------------------------------------------
# One parse per module, split back into a page per header by where each
# entity is declared.
gen "${WORK}/hdr" --layout header
assert_grep "T one(T x)" "${WORK}/hdr/Demo_Alpha_One_hpp.rst"
assert_no_grep "two(" "${WORK}/hdr/Demo_Alpha_One_hpp.rst"
assert_grep "int two(int x)" "${WORK}/hdr/Demo_Alpha_Two_hpp.rst"
assert_no_grep "one(" "${WORK}/hdr/Demo_Alpha_Two_hpp.rst"
assert_grep "DEMO_TWICE" "${WORK}/hdr/Demo_Alpha_Two_hpp.rst"
assert_grep "int good()" "${WORK}/hdr/Demo_Beta_Good_hpp.rst"

echo "PASS: run_gen_cpp_docs"
