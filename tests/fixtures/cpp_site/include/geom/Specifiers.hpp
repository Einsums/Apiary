// ----------------------------------------------------------------------------------------------
// Copyright (c) The Einsums Developers. All rights reserved.
// Licensed under the MIT License. See LICENSE.txt in the project root for license information.
// ----------------------------------------------------------------------------------------------
#pragma once

#include <geom/Templates.hpp>

// Fourth header of the cpp_site fixture: every part of a declaration beyond
// its name, type, and parameters. Requires-clauses (in the template head and
// trailing), exception specifications, constexpr/consteval, explicit,
// virtual/override/final, and = 0 / = delete / = default must all render,
// and overloads that differ only in a constraint must stay distinct.

namespace geom {

/// Double a scalar.
/// @param x The value.
template <typename T>
    requires Scalar<T>
T twice(T x);

/// Double something that is not a scalar, by concatenation.
/// @param x The value.
template <typename T>
    requires(!Scalar<T>)
T twice(T x);

/// The number of elements in anything with a ``size()``.
/// @param c The container.
template <typename C>
    requires requires(C const &c) { c.size(); }
int length(C const &c);

/// The number of elements in a built-in array.
/// @param c The array.
template <typename C>
    requires requires(C const &c) { sizeof(c) / sizeof(c[0]); }
int length(C const &c);

/// Widen a large scalar. The requires-expression holds only nested
/// requirements, so it declares as the conjunction of them.
/// @param x The value.
template <typename T>
    requires requires {
        requires Scalar<T>;
        requires(sizeof(T) > 4);
    }
Real widen(T x);

/// Absolute value, without throwing.
/// @param x The value.
constexpr Real magnitude(Real x) noexcept;

/// Swap two values, throwing only if moving them can.
/// @param a The first value.
/// @param b The second value.
template <typename T>
void exchange(T &a, T &b) noexcept(noexcept(T(static_cast<T &&>(a))));

/// A compile-time square.
/// @param n The value to square.
consteval int square(int n);

/// Circles cannot be compared by area.
bool same_area(Circle const &a, Circle const &b) = delete;

/// A closed shape in the plane.
class Shape {
  public:
    /// A default shape.
    Shape() = default;

    /// Shapes are not copied.
    Shape(Shape const &) = delete;

    /// Release the shape.
    virtual ~Shape() = default;

    /// The enclosed area.
    virtual Real area() const = 0;

    /// Whether the shape encloses any area.
    constexpr explicit operator bool() const noexcept;

    /// Shapes come from a pool.
    /// @param bytes The allocation size.
    static void *operator new(size_t bytes);
};

/// A regular hexagon, which nothing derives from.
class Hexagon final : public Shape {
  public:
    /// A hexagon with a given side.
    /// @param side The side length.
    explicit Hexagon(Real side);

    /// The enclosed area.
    Real area() const override;

    /// The side length, fixed for good.
    virtual Real side() const final;
};

/// A box holding one scalar.
/// @tparam T The scalar type.
template <typename T>
    requires Scalar<T>
class Box {
  public:
    /// Box a value, converting implicitly only from a scalar.
    /// @tparam U The source type.
    /// @param u The value.
    template <typename U>
    explicit(!Scalar<U>) Box(U u);

    /// Scale by a factor of any scalar type.
    /// @tparam U The factor type.
    /// @param factor The factor.
    template <typename U>
    Box scaled(U factor) const
        requires Scalar<U>;

    /// Whether the box holds an exact value.
    static constexpr bool IsExact = false;

    /// The value rounded to a whole number; only an inexact box has one.
    Box rounded() const
        requires(!IsExact);
};

/// A box of a scalar type.
/// @tparam T The scalar type.
template <typename T>
    requires Scalar<T>
using ScalarBox = Box<T>;

} // namespace geom
