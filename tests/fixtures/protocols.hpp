//----------------------------------------------------------------------------------------------
// Copyright (c) The Einsums Developers. All rights reserved.
// Licensed under the MIT License. See LICENSE.txt in the project root for license information.
//----------------------------------------------------------------------------------------------

// Fixture: the backend-neutral protocol directives (APIARY_BUFFER_PROTOCOL_STD,
// APIARY_INDEX_PROTOCOL_STD with a view, APIARY_ITERATOR_STD) and a
// std::shared_ptr holder. These are where pybind11 and nanobind differ in
// shape rather than in spelling, so the fixture pins both targets' output.
//
// The golden runs have no standard library, and the protocol code apiary
// writes passes std::vector arguments. The accessors that take them are
// member templates, so this header parses without <vector> and the compile
// check still instantiates them against the real calls.
//
// A buffer source on its own, with no APIARY_BUFFER_PROTOCOL, is deliberate:
// under pybind11 it must still produce the buffer_protocol() class flag.

#pragma once

#include <apiary/Annotations.hpp>

namespace einsums::fixture {

/// An index-expression slot with the shape the index protocol fills in.
struct Slice {
    enum class Kind : unsigned char { Index, Range, Full };

    Kind      kind  = Kind::Full;
    long long index = 0;
    long long start = 0;
    long long stop  = 0;
    long long step  = 1;
};

/// A strided view returned by Field's slicing.
template <typename T>
class APIARY_EXPOSE APIARY_BUFFER_PROTOCOL_STD(data = data, rank = rank, dim = dim, stride = stride, element_type = T)
    APIARY_INSTANTIATE_AS("FieldViewD", FieldView<double>) FieldView {
  public:
    APIARY_EXPOSE unsigned long rank() const;
    APIARY_EXPOSE unsigned long dim(unsigned long axis) const;
    APIARY_EXPOSE long long     stride(unsigned long axis) const;
    T                          *data();
};

/// A dense array exposing all three protocols.
template <typename T>
class APIARY_EXPOSE APIARY_BUFFER_PROTOCOL_STD(data = data, rank = rank, dim = dim, stride = stride, element_type = T)
    APIARY_ITERATOR_STD(begin = begin, end = end)
        APIARY_INDEX_PROTOCOL_STD(element_type = T, rank = rank, dim = dim, at_element = at, set_element = set, at_view = view,
                                  view_type = einsums::fixture::FieldView<T>, slice_spec = einsums::fixture::Slice)
            APIARY_INSTANTIATE_AS("FieldD", Field<double>) Field {
  public:
    APIARY_EXPOSE Field();
    APIARY_EXPOSE unsigned long rank() const;
    APIARY_EXPOSE unsigned long dim(unsigned long axis) const;
    APIARY_EXPOSE long long     stride(unsigned long axis) const;

    template <typename Index>
    T at(Index const &idx) const;
    template <typename Index>
    void set(Index const &idx, T value);
    template <typename Specs>
    FieldView<T> view(Specs const &specs) const;

    T *data();
    T *begin();
    T *end();
};

/// Shared ownership: a holder under pybind11, a caster under nanobind.
class APIARY_EXPOSE APIARY_HOLDER(std::shared_ptr) Node {
  public:
    APIARY_EXPOSE Node();
    APIARY_EXPOSE int id() const;
};

} // namespace einsums::fixture
