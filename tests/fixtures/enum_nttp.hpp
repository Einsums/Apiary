//----------------------------------------------------------------------------------------------
// Copyright (c) The Einsums Developers. All rights reserved.
// Licensed under the MIT License. See LICENSE.txt in the project root for license information.
//----------------------------------------------------------------------------------------------

// Fixture: an enum used as a non-type template parameter (NTTP). Exercises
// the enum-NTTP path — the instantiation directive names enumerators
// (``Layout::RowMajor``), which apiary rewrites to their fully-qualified
// form (``einsums::fixture::Layout::RowMajor``) so the generated bindings
// compile at module scope, and names each instantiation after the
// enumerator leaf (``Storage_float_RowMajor``).
//
// Covers both a class template (cross-product @instantiate over a type and
// an enum parameter) and a free-function template (@instantiate_as pinning
// the enum parameter).

#pragma once

#include <apiary/Annotations.hpp>

namespace einsums::fixture {

enum class APIARY_EXPOSE Layout : int {
    /// Row-major contiguous storage.
    RowMajor = 0,
    /// Column-major contiguous storage.
    ColumnMajor = 1,
};

/// Storage parameterized on element type and an enum memory layout.
template <typename T, Layout L>
class APIARY_EXPOSE APIARY_INSTANTIATE(Storage, T(float, double), L(Layout::RowMajor, Layout::ColumnMajor)) Storage {
  public:
    /// Default-construct empty storage.
    APIARY_EXPOSE Storage();

    /// Total element count.
    APIARY_EXPOSE int size() const;
};

/// Free-function template pinned per enum layout via @instantiate_as.
template <Layout L>
APIARY_EXPOSE APIARY_INSTANTIATE_AS("transpose_row", Layout::RowMajor)
    APIARY_INSTANTIATE_AS("transpose_col", Layout::ColumnMajor) int transpose(int x);

} // namespace einsums::fixture
