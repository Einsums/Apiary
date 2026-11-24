//----------------------------------------------------------------------------------------------
// Copyright (c) The Einsums Developers. All rights reserved.
// Licensed under the MIT License. See LICENSE.txt in the project root for license information.
//----------------------------------------------------------------------------------------------

// Single-entry dispatch fixture. Under pybind11, a Python name with four or more
// instantiations whose class-typed parameters tell them apart binds as one entry
// keyed on those parameters' exact types, with the ordered overload set kept under
// a private name as its fallback. Three shapes:
//
//   * fill       - four instantiations over Box<T>: one entry, keyed on `out` and `in`.
//   * fill_slow  - the same, opted out: the ordered overload set, as before.
//   * scale_by   - four instantiations differing only in a scalar type: nothing
//                  class-typed tells them apart, so they keep the ordered set.

#pragma once

#include <apiary/Annotations.hpp>

namespace einsums::fixture {

/// A value of type T in a box.
template <typename T>
struct APIARY_EXPOSE APIARY_INSTANTIATE_AS("BoxI", Box<int>) APIARY_INSTANTIATE_AS("BoxL", Box<long>)
APIARY_INSTANTIATE_AS("BoxF", Box<float>) APIARY_INSTANTIATE_AS("BoxD", Box<double>) Box {
    APIARY_EXPOSE   Box() = default;
    APIARY_EXPOSE T value{};
};

/// Write @p in times @p scale into @p out.
template <typename T>
APIARY_EXPOSE APIARY_INSTANTIATE_AS("fill", int) APIARY_INSTANTIATE_AS("fill", long) APIARY_INSTANTIATE_AS("fill", float)
APIARY_INSTANTIATE_AS("fill", double) void
fill(Box<T> *out, Box<T> const &in, double scale = 2.0) {
    if (out != nullptr) {
        out->value = static_cast<T>(in.value * scale);
    }
}

/// fill, kept on pybind11's ordered search: the reference the dispatcher's behaviour is checked against.
template <typename T>
APIARY_EXPOSE APIARY_NO_FAST_DISPATCH APIARY_INSTANTIATE_AS("fill_slow", int) APIARY_INSTANTIATE_AS("fill_slow", long)
APIARY_INSTANTIATE_AS("fill_slow", float) APIARY_INSTANTIATE_AS("fill_slow", double) void
fill_slow(Box<T> *out, Box<T> const &in, double scale = 2.0) {
    fill(out, in, scale);
}

/// A Box<double> with more in it: bound as a Python subclass of BoxD.
struct APIARY_EXPOSE Scaled : Box<double> {
    APIARY_EXPOSE        Scaled() = default;
    APIARY_EXPOSE double extra{};
};

// pybind11 takes the first overload whose casters accept, and a caster accepts a subclass, so a
// Scaled given to an overload set holding a BoxD overload ahead of the Scaled one reaches the BoxD
// overload. The dispatcher must agree: `which_base_first` leaves the Scaled row to the ordered
// search, and `which_derived_first` takes it only when the other arguments load without conversion.
// Each returns sizeof its box, naming the overload that ran; the _slow twins are the reference.

/// The overload that ran, as sizeof its box; BoxD's overload comes before Scaled's.
template <typename B>
APIARY_EXPOSE APIARY_INSTANTIATE_AS("which_base_first", einsums::fixture::Box<int>)
APIARY_INSTANTIATE_AS("which_base_first", einsums::fixture::Box<long>)
APIARY_INSTANTIATE_AS("which_base_first", einsums::fixture::Box<double>)
APIARY_INSTANTIATE_AS("which_base_first", einsums::fixture::Scaled) int
which_base_first([[maybe_unused]] B *box, [[maybe_unused]] double s) {
    return static_cast<int>(sizeof(B));
}

/// which_base_first on the ordered search.
template <typename B>
APIARY_EXPOSE APIARY_NO_FAST_DISPATCH APIARY_INSTANTIATE_AS("which_base_first_slow", einsums::fixture::Box<int>)
APIARY_INSTANTIATE_AS("which_base_first_slow", einsums::fixture::Box<long>)
APIARY_INSTANTIATE_AS("which_base_first_slow", einsums::fixture::Box<double>)
APIARY_INSTANTIATE_AS("which_base_first_slow", einsums::fixture::Scaled) int
which_base_first_slow(B *box, double s) {
    return which_base_first(box, s);
}

/// The overload that ran, as sizeof its box; Scaled's overload comes before BoxD's.
template <typename B>
APIARY_EXPOSE APIARY_INSTANTIATE_AS("which_derived_first", einsums::fixture::Box<int>)
APIARY_INSTANTIATE_AS("which_derived_first", einsums::fixture::Box<long>)
APIARY_INSTANTIATE_AS("which_derived_first", einsums::fixture::Scaled)
APIARY_INSTANTIATE_AS("which_derived_first", einsums::fixture::Box<double>) int
which_derived_first(B *box, double s) {
    return which_base_first(box, s);
}

/// which_derived_first on the ordered search.
template <typename B>
APIARY_EXPOSE APIARY_NO_FAST_DISPATCH APIARY_INSTANTIATE_AS("which_derived_first_slow", einsums::fixture::Box<int>)
APIARY_INSTANTIATE_AS("which_derived_first_slow", einsums::fixture::Box<long>)
APIARY_INSTANTIATE_AS("which_derived_first_slow", einsums::fixture::Scaled)
APIARY_INSTANTIATE_AS("which_derived_first_slow", einsums::fixture::Box<double>) int
which_derived_first_slow(B *box, double s) {
    return which_base_first(box, s);
}

/// A scalar behind an alias template: spelled differently per instantiation, so the codegen cannot
/// see it is not a class, and keys a column on it.
template <typename T>
using WeightOf = double;

// A column no instantiation has a registered class for keys nothing at run time; the dispatcher
// tells the rows apart by the others, and an omitted default for it does not send the call to the
// ordered search.

/// @p out's value plus @p w.
template <typename T>
APIARY_EXPOSE APIARY_INSTANTIATE_AS("weigh", int) APIARY_INSTANTIATE_AS("weigh", long) APIARY_INSTANTIATE_AS("weigh", float)
APIARY_INSTANTIATE_AS("weigh", double) double
weigh(Box<T> *out, WeightOf<T> w = 1.0) {
    return static_cast<double>(out->value) + w;
}

/// weigh on the ordered search.
template <typename T>
APIARY_EXPOSE APIARY_NO_FAST_DISPATCH APIARY_INSTANTIATE_AS("weigh_slow", int) APIARY_INSTANTIATE_AS("weigh_slow", long)
APIARY_INSTANTIATE_AS("weigh_slow", float) APIARY_INSTANTIATE_AS("weigh_slow", double) double
weigh_slow(Box<T> *out, WeightOf<T> w = 1.0) {
    return weigh(out, w);
}

/// @p x times @p y, for a scalar type the instantiations alone tell apart.
template <typename T>
APIARY_EXPOSE APIARY_INSTANTIATE_AS("scale_by", int) APIARY_INSTANTIATE_AS("scale_by", long) APIARY_INSTANTIATE_AS("scale_by", float)
APIARY_INSTANTIATE_AS("scale_by", double) double
scale_by(T x, double y) {
    return static_cast<double>(x) * y;
}

} // namespace einsums::fixture
