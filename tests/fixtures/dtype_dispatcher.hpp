//----------------------------------------------------------------------------------------------
// Copyright (c) The Einsums Developers. All rights reserved.
// Licensed under the MIT License. See LICENSE.txt in the project root for license information.
//----------------------------------------------------------------------------------------------

// Dtype-dispatcher fixture: same-Python-name + same-argument-signature
// + recognized scalar dtypes auto-collapse into a single Python def
// taking ``dtype="..."``. The codegen detects the pattern from the
// INSTANTIATE_AS directives below and emits one runtime if-chain
// (no manual ``APIARY_TEMPLATE_KWARGS`` needed for this case
// since there are no bool template parameters).

#pragma once

#include <apiary/Annotations.hpp>

namespace einsums::fixture {

/// Allocate-and-zero a scalar value, returned as the dtype's value
/// type. The four INSTANTIATE_AS lines all share the same (int) arg
/// signature and differ only in the return type, so the codegen
/// auto-detects the dtype-dispatcher pattern.
template <typename T>
APIARY_EXPOSE APIARY_INSTANTIATE_AS("zero_value", float) APIARY_INSTANTIATE_AS("zero_value", double) T
    zero_value(int seed);

// The dtype selector is appended AFTER the real arguments, so as soon as one of
// those carries a default the selector is a required parameter following an
// optional one - which Python cannot spell, and which used to be emitted
// verbatim. The result was a .pyi that failed to parse, costing the type
// checker every symbol in the file rather than just this function. That is
// Finding 12, and it shipped: einsums/graph.pyi was unreadable to pyright for
// exactly this reason, via the member form below.
//
// Nothing in the corpus had a defaulted argument in front of a dispatcher, so
// nothing caught it. Both forms are here now.

/// Free-function dispatcher with a defaulted argument ahead of the selector.
template <typename T>
APIARY_EXPOSE APIARY_INSTANTIATE_AS("scaled_zero", float) APIARY_INSTANTIATE_AS("scaled_zero", double) T
    scaled_zero(int seed, bool normalize = true);

/// The member form, which is the shape that actually shipped broken.
class APIARY_EXPOSE Allocator {
  public:
    APIARY_EXPOSE Allocator();

    /// Allocate a named buffer. `intermediate` has a default and precedes the
    /// dtype selector, exactly as Graph::create_tensor does.
    template <typename T>
    APIARY_EXPOSE APIARY_INSTANTIATE_MEMBER_AS("allocate", T = float)
        APIARY_INSTANTIATE_MEMBER_AS("allocate", T = double) T allocate(int slot, bool intermediate = true);
};

} // namespace einsums::fixture
