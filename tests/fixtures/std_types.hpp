//----------------------------------------------------------------------------------------------
// Copyright (c) The Einsums Developers. All rights reserved.
// Licensed under the MIT License. See LICENSE.txt in the project root for license information.
//----------------------------------------------------------------------------------------------

// Standard-library type fixture: the templates TypeTranslator maps specially.
//
// TypeTranslator recognises std types by their CANONICAL SPELLING, not by
// their definition, so forward declarations are enough - and they keep the
// goldens hermetic. The suite compiles fixtures with -nostdinc++ precisely so
// a golden never depends on which standard library the host happens to have;
// pulling in the real <variant> would trade that away for nothing.

#pragma once

#include <apiary/Annotations.hpp>

// Declaration-only stand-ins. Only the names and template shapes matter: the
// generated code is never compiled, and TypeTranslator only ever sees the
// printed canonical type.
namespace std {
template <typename T>
class optional;
template <typename... Ts>
class variant;
template <typename T>
class function;
template <typename T>
class vector;
template <typename T, typename U>
struct pair;
template <typename T>
class shared_ptr;
} // namespace std

namespace einsums::fixture {

/// `std::optional<T>` parameter and return, which Python spells `T | None`.
APIARY_EXPOSE std::optional<double> maybe_scale(std::optional<double> value);

/// `std::variant<...>`, which Python spells as a union of its alternatives.
APIARY_EXPOSE double measure(std::variant<int, double> quantity);

/// `std::function<R(Args...)>`, which Python spells `Callable[[...], R]`.
/// The callable's own signature has to be translated element by element.
APIARY_EXPOSE void for_each(std::vector<double> const &values, std::function<void(double)> visit);

/// A container of a container, to prove the mapping recurses.
APIARY_EXPOSE std::vector<std::vector<double>> transpose(std::vector<std::vector<double>> const &rows);

/// `std::pair`, which Python spells as a fixed-length tuple.
APIARY_EXPOSE std::pair<double, int> split(double value);

/// A smart pointer, which Python spells as the pointee.
APIARY_EXPOSE std::shared_ptr<std::vector<double>> share(std::vector<double> values);

} // namespace einsums::fixture
