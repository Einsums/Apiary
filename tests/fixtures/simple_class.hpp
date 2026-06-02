//----------------------------------------------------------------------------------------------
// Copyright (c) The Einsums Developers. All rights reserved.
// Licensed under the MIT License. See LICENSE.txt in the project root for license information.
//----------------------------------------------------------------------------------------------

// Phase-2 fixture: a class with constructors, methods, fields, operator
// overload, getter/setter pair, and one hidden inherited member. Exercises
// scope tracking and the BoundClass / BoundMethod / BoundField paths.

#pragma once

#include <apiary/Annotations.hpp>

namespace einsums::fixture {

class APIARY_EXPOSE APIARY_RENAME("PyShape") APIARY_NOCOPY Shape {
  public:
    /// Default-construct an empty shape.
    APIARY_EXPOSE Shape();

    /// Build a shape from a single dimension.
    APIARY_EXPOSE explicit Shape(int dim);

    /// Number of dimensions.
    APIARY_EXPOSE APIARY_RVP(reference_internal) int rank() const;

    /// Equality across two shapes.
    APIARY_EXPOSE APIARY_OPERATOR("__eq__") bool operator==(Shape const &other) const;

    /// Read-only access to the contained dim, exposed as a Python property.
    APIARY_GETTER("dim") int get_dim() const;

    /// Mutator paired with the getter above.
    APIARY_SETTER("dim") void set_dim(int value);

    APIARY_EXPOSE int public_field;

    APIARY_HIDE void internal_helper();
};

} // namespace einsums::fixture
