//----------------------------------------------------------------------------------------------
// Copyright (c) The Einsums Developers. All rights reserved.
// Licensed under the MIT License. See LICENSE.txt in the project root for license information.
//----------------------------------------------------------------------------------------------

#pragma once

// A small dense array type that exposes Python's container protocols through
// Apiary's backend-neutral directives:
//
//   APIARY_BUFFER_PROTOCOL_STD  memoryview(g), numpy.asarray(g), zero-copy
//   APIARY_INDEX_PROTOCOL_STD   g[i], g[i, j], g[1:3], g[:, 0], g[...]
//   APIARY_ITERATOR_STD         for x in g
//
// Nothing here includes pybind11 or nanobind: the directives name plain C++
// member functions, and Apiary writes the protocol code for whichever binding
// library the build selects.

#include <apiary/Annotations.hpp>

#include <complex>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace grid {

/// Raised for a malformed request, such as an index with the wrong number of axes.
class APIARY_EXPOSE APIARY_EXCEPTION GridError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

/// One axis of an index expression, as the index protocol hands it to Grid::view.
///
/// Negative indices and slice bounds arrive already normalized against the axis length.
struct SliceSpec {
    enum class Kind : std::uint8_t { Index, Range, Full };

    Kind         kind  = Kind::Full;
    std::int64_t index = 0;
    std::int64_t start = 0;
    std::int64_t stop  = 0;
    std::int64_t step  = 1;
};

namespace detail {

template <typename T>
struct Layout {
    std::shared_ptr<std::vector<T>> storage;
    std::size_t                     offset = 0;
    std::vector<std::size_t>        shape;
    std::vector<std::int64_t>       strides; // in elements

    [[nodiscard]] std::size_t flat(std::vector<std::int64_t> const &idx) const {
        if (idx.size() != shape.size()) {
            throw GridError("expected " + std::to_string(shape.size()) + " indices, got " + std::to_string(idx.size()));
        }
        std::int64_t pos = static_cast<std::int64_t>(offset);
        for (std::size_t i = 0; i < idx.size(); ++i) {
            pos += idx[i] * strides[i];
        }
        return static_cast<std::size_t>(pos);
    }

    [[nodiscard]] Layout sliced(std::vector<SliceSpec> const &specs) const {
        if (specs.size() != shape.size()) {
            throw GridError("expected " + std::to_string(shape.size()) + " slice specs, got " + std::to_string(specs.size()));
        }
        Layout out;
        out.storage      = storage;
        std::int64_t pos = static_cast<std::int64_t>(offset);
        for (std::size_t i = 0; i < specs.size(); ++i) {
            SliceSpec const &s = specs[i];
            switch (s.kind) {
            case SliceSpec::Kind::Index:
                pos += s.index * strides[i];
                break;
            case SliceSpec::Kind::Range: {
                std::int64_t const n = s.step > 0 ? (s.stop - s.start + s.step - 1) / s.step : (s.start - s.stop - s.step - 1) / -s.step;
                pos += s.start * strides[i];
                out.shape.push_back(static_cast<std::size_t>(n > 0 ? n : 0));
                out.strides.push_back(strides[i] * s.step);
                break;
            }
            case SliceSpec::Kind::Full:
                out.shape.push_back(shape[i]);
                out.strides.push_back(strides[i]);
                break;
            }
        }
        out.offset = static_cast<std::size_t>(pos);
        return out;
    }
};

} // namespace detail

/// A strided view into a Grid's storage. Keeps the storage alive on its own.
template <typename T>
class APIARY_EXPOSE APIARY_BUFFER_PROTOCOL_STD(data = data, rank = rank, dim = dim, stride = stride, element_type = T)
    APIARY_INSTANTIATE_AS("GridViewD", GridView<double>) APIARY_INSTANTIATE_AS("GridViewZ", GridView<std::complex<double>>) GridView {
  public:
    explicit GridView(detail::Layout<T> layout) : _layout(std::move(layout)) {}

    /// Number of axes.
    APIARY_EXPOSE std::size_t rank() const { return _layout.shape.size(); }

    /// Length of axis ``axis``.
    APIARY_EXPOSE std::size_t dim(std::size_t axis) const { return _layout.shape.at(axis); }

    /// Distance between neighbours along ``axis``, in elements.
    APIARY_EXPOSE std::int64_t stride(std::size_t axis) const { return _layout.strides.at(axis); }

    /// The element at a full index.
    APIARY_EXPOSE T at(std::vector<std::int64_t> const &idx) const { return (*_layout.storage)[_layout.flat(idx)]; }

    T *data() { return _layout.storage->data() + _layout.offset; }

  private:
    detail::Layout<T> _layout;
};

/// A dense, row-major array of any rank.
template <typename T>
class APIARY_EXPOSE APIARY_BUFFER_PROTOCOL_STD(data = data, rank = rank, dim = dim, stride = stride, element_type = T)
    APIARY_ITERATOR_STD(begin = begin, end = end)
        APIARY_INDEX_PROTOCOL_STD(element_type = T, rank = rank, dim = dim, at_element = at, set_element = set, at_view = view,
                                  view_type = grid::GridView<T>, slice_spec = grid::SliceSpec)
            APIARY_INSTANTIATE_AS("GridD", Grid<double>) APIARY_INSTANTIATE_AS("GridZ", Grid<std::complex<double>>) Grid {
  public:
    /// A zero-filled grid of the given shape.
    APIARY_EXPOSE explicit Grid(std::vector<std::size_t> shape) {
        _layout.shape = std::move(shape);
        _layout.strides.assign(_layout.shape.size(), 1);
        std::size_t n = 1;
        for (std::size_t i = _layout.shape.size(); i-- > 0;) {
            _layout.strides[i] = static_cast<std::int64_t>(n);
            n *= _layout.shape[i];
        }
        _layout.storage = std::make_shared<std::vector<T>>(n, T{});
    }

    /// Number of axes.
    APIARY_EXPOSE std::size_t rank() const { return _layout.shape.size(); }

    /// Length of axis ``axis``.
    APIARY_EXPOSE std::size_t dim(std::size_t axis) const { return _layout.shape.at(axis); }

    /// Distance between neighbours along ``axis``, in elements.
    APIARY_EXPOSE std::int64_t stride(std::size_t axis) const { return _layout.strides.at(axis); }

    /// Total number of elements.
    APIARY_EXPOSE std::size_t size() const { return storage().size(); }

    /// Set every element to ``value``.
    APIARY_EXPOSE void fill(T value) {
        for (T &x : storage()) {
            x = value;
        }
    }

    /// Drop this grid's storage. Views taken earlier keep theirs; the grid
    /// itself refuses every later access, a buffer export included.
    APIARY_EXPOSE void release() { _layout.storage.reset(); }

    T at(std::vector<std::int64_t> const &idx) const { return storage()[_layout.flat(idx)]; }
    void set(std::vector<std::int64_t> const &idx, T value) { storage()[_layout.flat(idx)] = value; }
    GridView<T> view(std::vector<SliceSpec> const &specs) const {
        storage();
        return GridView<T>(_layout.sliced(specs));
    }

    T *data() { return storage().data(); }
    T *begin() { return storage().data(); }
    T *end() { return storage().data() + storage().size(); }

  private:
    std::vector<T> &storage() const {
        if (!_layout.storage) {
            throw GridError("grid has been released");
        }
        return *_layout.storage;
    }

    detail::Layout<T> _layout;
};

} // namespace grid
