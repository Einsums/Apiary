//----------------------------------------------------------------------------------------------
// Copyright (c) The Einsums Developers. All rights reserved.
// Licensed under the MIT License. See LICENSE.txt in the project root for license information.
//----------------------------------------------------------------------------------------------

// Parameter-pack fixture, deliberately separate from signatures_modern.hpp.
//
// A pack has no arity in the declaration; APIARY_INSTANTIATE_AS supplies one.
// Both emitters have to expand it, and neither does:
//
// KNOWN BAD (Finding 10a, the binding): the pack is re-emitted verbatim, so
// the disambiguating cast comes out as `double (*)(double...)`. That is not
// "three doubles" to a C++ compiler, it is C varargs - clang reads it as
// `double (*)(double, ...)` and rejects the cast against
// `sum_all<double, double, double>`. The generated TU does not compile.
// Verified by compiling the emitted cast against the declaration.
//
// KNOWN BAD (Finding 10b, the stub): the same verbatim spelling reaches the
// .pyi as `def sum_all(values: double...)`, which is not valid Python, so the
// whole stub fails to parse and the type checker it exists to feed gets
// nothing.
//
// 10b is why this fixture is registered ONLY in run_golden.sh and not in
// run_pyi_golden.sh. That runner asserts every generated stub parses as
// Python - the assertion that found 10b in the first place - so goldening this
// stub would mean committing a file the assertion must then reject. The .cpp
// golden still pins the shape, including the broken cast, so the fix will show
// up as a golden diff. Register this fixture in run_pyi_golden.sh as part of
// fixing 10b.

#pragma once

#include <apiary/Annotations.hpp>

namespace einsums::fixture {

/// Sum a parameter pack, pinned to three doubles by the instantiation.
template <typename... Ts>
APIARY_EXPOSE APIARY_INSTANTIATE_AS("sum_all", double, double, double) double sum_all(Ts... values);

/// A pack behind a leading fixed parameter, so the expansion has to start
/// after an argument that is not part of it.
template <typename... Ts>
APIARY_EXPOSE APIARY_INSTANTIATE_AS("scaled_sum", double, double) double scaled_sum(double scale, Ts... values);

} // namespace einsums::fixture
