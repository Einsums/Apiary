//----------------------------------------------------------------------------------------------
// Copyright (c) The Einsums Developers. All rights reserved.
// Licensed under the MIT License. See LICENSE.txt in the project root for license information.
//----------------------------------------------------------------------------------------------

// Parameter-pack fixture.
//
// A pack has no arity in the declaration; APIARY_INSTANTIATE_AS supplies one,
// and both emitters have to expand it. Neither did (Finding 10), and the same
// verbatim `Ts...` was a different disaster in each target:
//
//   the binding  `double...` in a C++ cast is VARARGS - clang reads
//                `double (*)(double, ...)`, the cast names no function, and the
//                generated TU does not compile.
//   the stub     `def sum_all(values: double...)` is not valid Python, so the
//                whole .pyi fails to parse and the type checker gets nothing
//                from the file - not just nothing for this function.
//
// Template parameters before the pack take one argument each and the pack takes
// the rest, which is the only arrangement a function template's pack can have:
// it must be last to be deducible. scaled_sum below is the case that proves the
// expansion starts after a fixed parameter rather than at argument zero.

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
