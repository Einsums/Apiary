// ----------------------------------------------------------------------------------------------
// Copyright (c) The Einsums Developers. All rights reserved.
// Licensed under the MIT License. See LICENSE.txt in the project root for license information.
// ----------------------------------------------------------------------------------------------
#pragma once

// Fixture for the per-entity site renderer (apiary_render_cpp_site.py):
// a class with a const/non-const overload pair, an enum, a namespace-scope
// alias, a concept, and a forward declaration of a free function that
// Ops.hpp defines (the overload set must merge across the two headers).

namespace geom {

/// The floating-point type used by every shape.
using Real = double;

/// How a shape is rendered.
enum class Style {
    /// Only the outline.
    Outline,
    /// Filled with the current color.
    Filled,
};

/// A type usable as a scalar coordinate.
template <typename T>
concept Scalar = requires(T a) { a + a; };

/// A circle in the plane.
class Circle {
  public:
    /// Construct a unit circle at the origin.
    Circle();

    /// The radius.
    Real radius() const;

    /// Set the radius.
    /// @param r The new radius.
    void radius(Real r);

    /// The enclosed area.
    Real area() const;

    /// The center, as an lvalue. The ``&``/``&&`` pair below collapses onto one
    /// signature if the renderer drops the ref-qualifier, exactly the way a
    /// const/non-const pair does without ``const``.
    Real &center() &;

    /// The center, moved out of an expiring circle.
    Real &&center() &&;

    /// The center of a const circle.
    Real const &center() const &;
};

/// Uniformly scale a circle. Defined in Ops.hpp; this forward declaration
/// must merge into the same overload-set page.
Circle scale(Circle const &c, Real factor);

/// The most dimensions a shape can have.
inline constexpr int max_rank = 3;

/// The bytes that start a saved shape. An array declares its name inside
/// its type.
inline constexpr char magic[4] = {'G', 'E', 'O', '\0'}; // NOLINT(modernize-avoid-c-arrays)

/// Whether @p T is a circle.
/// @tparam T The type to test.
template <typename T>
inline constexpr bool is_circle_v = false;

/// A circle is one. A specialization is a value of the template, not a
/// variable of its own, so it is not documented separately.
template <>
inline constexpr bool is_circle_v<Circle> = true;

} // namespace geom
