// ----------------------------------------------------------------------------------------------
// Copyright (c) The Einsums Developers. All rights reserved.
// Licensed under the MIT License. See LICENSE.txt in the project root for license information.
// ----------------------------------------------------------------------------------------------
#pragma once

#include <geom/Shapes.hpp>

// Second header of the cpp_site fixture: free-function overloads, an
// operator (which lands on the operators catch-all page), and a
// function-like macro.

/// Alignment used by the fixture allocator helpers.
#define GEOM_ALIGNMENT 64

/// Clamp a value into a range.
#define GEOM_CLAMP(x, lo, hi) ((x) < (lo) ? (lo) : ((x) > (hi) ? (hi) : (x)))

namespace geom {

/// Uniformly scale a circle.
/// @param c The circle to scale.
/// @param factor The scale factor.
/// @return The scaled circle.
Circle scale(Circle const &c, Real factor);

/// Scale by an integer factor.
Circle scale(Circle const &c, int factor);

/// Whether two circles have the same radius.
bool operator==(Circle const &a, Circle const &b);

/// The union-ish combination of two circles.
Circle operator+(Circle const &a, Circle const &b);

} // namespace geom
