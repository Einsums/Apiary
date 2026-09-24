// ----------------------------------------------------------------------------------------------
// Copyright (c) The Einsums Developers. All rights reserved.
// Licensed under the MIT License. See LICENSE.txt in the project root for license information.
// ----------------------------------------------------------------------------------------------
#pragma once

#include <geom/Shapes.hpp>

// Third header of the cpp_site fixture: one of each kind of template
// parameter, on a free function, a class template, a member function
// template, and an alias template. Every parameter must render as declared,
// not as ``typename <name>``.

namespace geom {

/// The unsigned type of sizes and extents.
using size_t = decltype(sizeof(0));

/// A fixed-rank grid of values.
/// @tparam T The element type.
/// @tparam Rank The number of dimensions.
template <typename T, size_t Rank = 2>
class Grid {
  public:
    /// Build an empty grid.
    Grid();

    /// Fill every cell from a generator.
    /// @tparam F The generator type.
    /// @tparam Unroll Whether to unroll the fill loop.
    /// @param f The generator.
    template <typename F, bool Unroll = false>
    void fill(F const &f);

    /// Reshape to another rank.
    /// @tparam NewRank The rank of the result.
    /// @tparam Dims The extents, one per dimension.
    template <size_t NewRank, typename... Dims>
    Grid<T, NewRank> reshape(Dims... dims) const;
};

/// A grid that grows one layer at a time. Its base and its method name the
/// non-type parameter ``Rank``, which must print as ``Rank``, never qualified
/// by the class (``Stack::Rank`` is not C++).
/// @tparam T The element type.
/// @tparam Rank The number of dimensions.
template <typename T, size_t Rank>
class Stack : public Grid<T, Rank> {
  public:
    /// The top layer.
    Grid<T, Rank> top() const;
};

/// Return the mode-``mode`` unfolding of a grid.
/// @tparam mode The mode to unfold along.
/// @tparam CRank The rank of the source.
/// @tparam T The element type.
/// @param source The grid to unfold.
template <unsigned int mode, size_t CRank, typename T>
Grid<T, 2> unfold(Grid<T, CRank> const &source);

/// Sum any number of scalars.
/// @tparam Args The argument types.
template <typename... Args>
Real sum(Args... args);

/// A grid whose extents are fixed at compile time.
/// @tparam Ns The extents.
template <size_t... Ns>
Grid<Real, sizeof...(Ns)> make_fixed();

/// Rebuild a grid with another container template.
/// @tparam TT The container template.
template <template <typename, size_t> typename TT>
TT<Real, 2> rebuild(Grid<Real, 2> const &g);

/// Repack a grid into another container template.
/// @tparam Container The container template.
template <template <typename Elem, size_t Extent> typename Container>
Container<Real, 3> repack(Grid<Real, 2> const &g);

/// Advance a coordinate by a compile-time step.
/// @tparam Step The step, of any scalar type.
/// @param x The coordinate.
template <Scalar auto Step>
Real advance(Real x);

/// Scale every cell by a scalar.
/// @tparam S A scalar type.
/// @tparam Offset An additive offset applied after scaling.
template <Scalar S, int Offset = 0>
Grid<S, 2> scaled(Grid<S, 2> const &g, S factor);

/// Print any number of values, with an explicit separator type.
/// @tparam Sep The separator type.
template <typename Sep = char>
void print(Sep sep, auto const &...values);

/// A square grid.
/// @tparam T The element type.
/// @tparam N The side length.
template <typename T, size_t N = 3>
using Square = Grid<T, 2>;

} // namespace geom
