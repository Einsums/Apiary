//----------------------------------------------------------------------------------------------
// Copyright (c) The Einsums Developers. All rights reserved.
// Licensed under the MIT License. See LICENSE.txt in the project root for license information.
//----------------------------------------------------------------------------------------------

// Phase C.8 fixture: leading bool template parameters lifted into Python
// kwargs via APIARY_TEMPLATE_KWARGS, with per-dtype slices supplied
// by APIARY_INSTANTIATE_BOOLS. Each BOOLS directive expands
// internally to 2^N INSTANTIATE_AS lines covering every false/true combo
// in lexicographic order; same-tail instantiations collapse into one
// m.def with a runtime if-chain dispatcher.

#pragma once

#include <apiary/Annotations.hpp>

namespace einsums::fixture {

template <typename T>
struct Box {
    T value;
};

/// Apply a flagged transform to a value plus a Box payload.
template <bool TransA, bool TransB, typename T>
APIARY_EXPOSE APIARY_TEMPLATE_KWARGS("trans_a", "trans_b") APIARY_INSTANTIATE_BOOLS("apply", float)
    APIARY_INSTANTIATE_BOOLS("apply", double) T apply(T const x, Box<T> &b);

/// Single-bool void-returning variant.
template <bool Conjugate, typename T>
APIARY_EXPOSE APIARY_TEMPLATE_KWARGS("conjugate") APIARY_INSTANTIATE_BOOLS("scale_inplace", float)
    APIARY_INSTANTIATE_BOOLS("scale_inplace", double) void scale_inplace(Box<T> &b, T const factor);

/// Single-bool variant whose flag defaults to true: the kwarg takes the
/// template parameter's declared default.
template <bool Normalize = true, typename T>
APIARY_EXPOSE APIARY_TEMPLATE_KWARGS("normalize") APIARY_INSTANTIATE_BOOLS("rescale", float)
    APIARY_INSTANTIATE_BOOLS("rescale", double) void rescale(Box<T> &b);

template <typename T>
inline constexpr bool clamps_by_default = true;

/// Mixed defaults that clang has to evaluate: ``Wrap`` has none, so it is
/// False; ``Clamp`` names a constexpr variable that is true, so it is True.
template <bool Wrap, bool Clamp = clamps_by_default<float>, typename T>
APIARY_EXPOSE APIARY_TEMPLATE_KWARGS("wrap", "clamp") APIARY_INSTANTIATE_BOOLS("bound", float) void bound(Box<T> &b);

} // namespace einsums::fixture
