//----------------------------------------------------------------------------------------------
// Copyright (c) The Einsums Developers. All rights reserved.
// Licensed under the MIT License. See LICENSE.txt in the project root for license information.
//----------------------------------------------------------------------------------------------

// Operator fixture: overloaded operators and conversion operators.
//
// Operators are the one place where the C++ name and the Python name have no
// syntactic relationship - `operator+` has to become `__add__`, and a
// conversion operator has no name at all, only a target type. APIARY_OPERATOR
// carries the Python name explicitly, so what this fixture pins is that the
// emitter routes through it rather than mangling the C++ spelling.

#pragma once

#include <apiary/Annotations.hpp>

namespace einsums::fixture {

/// A value type with arithmetic, comparison and conversion operators.
class APIARY_EXPOSE Money {
  public:
    APIARY_EXPOSE Money();
    APIARY_EXPOSE explicit Money(double amount);

    /// Member operator: addition.
    APIARY_EXPOSE APIARY_OPERATOR("__add__") Money operator+(Money const &rhs) const;

    /// Member operator: in-place addition, which Python expects to return self.
    APIARY_EXPOSE APIARY_OPERATOR("__iadd__") Money &operator+=(Money const &rhs);

    /// Unary negation - same spelling as binary minus, different arity.
    APIARY_EXPOSE APIARY_OPERATOR("__neg__") Money operator-() const;

    /// Comparison. C++20 would usually spell this `<=>`; the individual
    /// operators are what Python needs.
    APIARY_EXPOSE APIARY_OPERATOR("__eq__") bool operator==(Money const &rhs) const;
    /// Ordering.
    APIARY_EXPOSE APIARY_OPERATOR("__lt__") bool operator<(Money const &rhs) const;

    /// Subscript, which Python spells `__getitem__`.
    APIARY_EXPOSE APIARY_OPERATOR("__getitem__") double operator[](int index) const;

    /// Call operator, which Python spells `__call__`.
    APIARY_EXPOSE APIARY_OPERATOR("__call__") double operator()(double scale) const;

    /// Explicit conversion to bool - Python's `__bool__`. A conversion
    /// operator has no name of its own, only a target type.
    APIARY_EXPOSE APIARY_OPERATOR("__bool__") explicit operator bool() const;

    /// Implicit conversion to double, for the non-explicit comparison.
    APIARY_EXPOSE APIARY_OPERATOR("__float__") operator double() const;
};

// A C++ name that is not a Python identifier - a free `operator*`, a member
// `operator-` with no APIARY_OPERATOR to name it - is REFUSED rather than
// emitted, and the refusal is reported. Those cases live in
// run_diagnostics.sh: a fixture that deliberately produces NO output cannot
// also pin emitted output, and every runner here drives the whole corpus.

} // namespace einsums::fixture
