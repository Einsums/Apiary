//----------------------------------------------------------------------------------------------
// Copyright (c) The Einsums Developers. All rights reserved.
// Licensed under the MIT License. See LICENSE.txt in the project root for license information.
//----------------------------------------------------------------------------------------------

// C++20 signature fixture: concepts, requires-clauses, and dependent types.
// Parameter packs live in variadic_pack.hpp - see there for why.
//
// apiary is a C++20 tool used by a C++20 library, and none of these shapes had
// a fixture. Each one puts something in the signature that is NOT part of the
// binding: a constraint the emitter must drop but the docs renderer must keep,
// a parameter pack that has no arity until it is instantiated, a type that
// cannot be resolved until T is known.

#pragma once

#include <apiary/Annotations.hpp>

namespace einsums::fixture {

/// Anything that can be added to itself. Concepts need no annotation: in docs
/// mode every concept in a module header is collected, and in binding mode a
/// concept produces nothing at all.
template <typename T>
concept Addable = requires(T a, T b) { a + b; };

/// Constrained via a terse template-parameter. The constraint belongs in the
/// C++ reference and must not reach the Python signature.
template <Addable T>
APIARY_EXPOSE APIARY_INSTANTIATE_AS("total", double) T total(T const *values, int count);

/// The same constraint written as a trailing requires-clause, which is a
/// different AST shape reaching the same place.
template <typename T>
    requires Addable<T>
APIARY_EXPOSE APIARY_INSTANTIATE_AS("total_checked", double) T total_checked(T const *values, int count);

/// A dependent type as both parameter and return: `typename T::ValueType`
/// cannot be resolved until T is pinned, so the emitter has to carry the
/// spelling through to the instantiation rather than resolve it early.
template <typename T>
struct Traits {
    using ValueType = T;
};

/// Takes and returns a dependent type.
template <typename T>
APIARY_EXPOSE APIARY_INSTANTIATE_AS("identity", double) typename Traits<T>::ValueType
identity(typename Traits<T>::ValueType v);

/// `auto` return type, deduced from the body. The declaration alone does not
/// say what comes back, so anything reading the return type off the
/// declaration has to cope.
APIARY_EXPOSE auto midpoint(double a, double b) -> double;

} // namespace einsums::fixture
