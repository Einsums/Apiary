//----------------------------------------------------------------------------------------------
// Copyright (c) The Einsums Developers. All rights reserved.
// Licensed under the MIT License. See LICENSE.txt in the project root for license information.
//----------------------------------------------------------------------------------------------

#pragma once

#include <apiary/Annotations.hpp>

#include <cmath>

namespace mathx {

/// A 2-D vector with the usual operations.
///
/// The compiled core of the ``mathx`` package. The hand-written Python layer
/// (``mathx`` / ``mathx.extras``) builds convenience helpers on top of it.
/// @since 1.0.0
class APIARY_EXPOSE APIARY_RENAME("Vec2") Vec2 {
  public:
    /// The zero vector.
    APIARY_EXPOSE Vec2() : _x(0.0), _y(0.0) {}

    /// A vector with the given components.
    APIARY_EXPOSE Vec2(double x, double y) : _x(x), _y(y) {}

    /// The Euclidean length ``sqrt(x*x + y*y)``.
    APIARY_EXPOSE double length() const { return std::sqrt(_x * _x + _y * _y); }

    /// A copy of this vector scaled to unit length.
    APIARY_EXPOSE Vec2 normalized() const {
        double const n = length();
        return Vec2(_x / n, _y / n);
    }

    /// A copy of this vector scaled by ``factor``.
    APIARY_EXPOSE Vec2 scaled(double factor) const { return Vec2(_x * factor, _y * factor); }

    /// The ``x`` component, exposed as a read/write property.
    APIARY_GETTER("x") double x() const { return _x; }
    APIARY_SETTER("x") void set_x(double value) { _x = value; }

    /// The ``y`` component, exposed as a read/write property.
    APIARY_GETTER("y") double y() const { return _y; }
    APIARY_SETTER("y") void set_y(double value) { _y = value; }

  private:
    double _x;
    double _y;
};

/// The dot product of two vectors.
APIARY_EXPOSE inline double dot(Vec2 const &a, Vec2 const &b) { return a.x() * b.x() + a.y() * b.y(); }

} // namespace mathx
