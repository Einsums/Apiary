//----------------------------------------------------------------------------------------------
// Copyright (c) The Einsums Developers. All rights reserved.
// Licensed under the MIT License. See LICENSE.txt in the project root for license information.
//----------------------------------------------------------------------------------------------

// Phase-4 fixture: a templated class with a cross-product instantiation
// list and a single explicit @instantiate_as override. Exercises the
// template instantiation path: explicit ``template class ...`` at TU
// scope plus a per-instantiation ``py::class_<>`` block, with method
// pointers retargeted to the concrete type.

#pragma once

#include <apiary/Annotations.hpp>

namespace einsums::fixture {

template <typename T, int N>
class APIARY_EXPOSE APIARY_INSTANTIATE(Tensor, T(float, double), N(1, 2)) Tensor {
  public:
    /// Default-construct an empty tensor.
    APIARY_EXPOSE Tensor();

    /// Total element count.
    APIARY_EXPOSE int size() const;

    /// Read-only rank accessor.
    APIARY_EXPOSE int rank() const;
};

template <typename T, int N>
class APIARY_EXPOSE APIARY_INSTANTIATE_AS("View2d", View<double, 2>) View {
  public:
    APIARY_EXPOSE View();

    APIARY_EXPOSE int stride(int dim) const;
};

// Phase-8b: cross-product with INSTANTIATE_TEMPLATE — placeholders use
// the C++ template-parameter names directly.
//
// Phase-8c: VARIADIC_FROM expands the pack ctor per instantiation; each
// Block_<E>_<R> gets a ctor with R typed slots.
template <typename Element, int Rank>
class APIARY_EXPOSE APIARY_INSTANTIATE_TEMPLATE("Block_{Element}_{Rank}", Block, Element(float, double), Rank(1, 2)) Block {
  public:
    APIARY_EXPOSE Block();

    template <typename... Dims>
    APIARY_EXPOSE APIARY_VARIADIC_FROM(Rank, int) Block(Dims... dims);

    APIARY_EXPOSE int size() const;
};

} // namespace einsums::fixture
