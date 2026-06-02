//----------------------------------------------------------------------------------------------
// Copyright (c) The Einsums Developers. All rights reserved.
// Licensed under the MIT License. See LICENSE.txt in the project root for license information.
//----------------------------------------------------------------------------------------------

#pragma once

// APIARY_* — Python binding annotation macros.
//
// These macros tag C++ declarations with metadata for the apiary
// codegen tool (see tools/apiary). Under Clang (and Clang-based
// drivers like ``icpx`` that define ``__clang__``), they expand to
// ``[[clang::annotate("apiary:...")]]`` which the tool reads from
// the AST. Under GCC the same C++11 attribute would emit a noisy
// ``-Wattributes`` for every site ("scoped attribute directive ignored"),
// so the macro collapses to nothing there. The codegen still sees the
// attribute because the libtooling driver behind ``apiary`` is
// libclang and always defines ``__clang__``; production GCC builds carry
// no overhead and no warnings.
//
// Placement: each macro is a C++11 attribute and goes between the
// class-key and the class name (or before the function return type).
// Multiple macros stack:
//
//     class APIARY_EXPOSE
//           APIARY_RENAME("PyTensor")
//           APIARY_HOLDER(std::shared_ptr)
//     Tensor { ... };
//
//     APIARY_EXPOSE
//     APIARY_RVP(reference_internal)
//     int Tensor::do_thing(int x);
//
// Every macro builds an ``"apiary:<directive>[:<args>]"`` payload
// via adjacent string-literal concatenation, which the codegen tool
// splits on ``:`` to recover the directive and arguments.

// ---------------------------------------------------------------------------
// Implementation detail
// ---------------------------------------------------------------------------

#if defined(__clang__)
#    define APIARY_DETAIL_ANNOTATE(payload) [[clang::annotate("apiary:" payload)]]
#else
#    define APIARY_DETAIL_ANNOTATE(payload) /* no-op on non-Clang compilers */
#endif

// ---------------------------------------------------------------------------
// Exposure
// ---------------------------------------------------------------------------

// Mark a declaration for binding. Without this, the codegen tool ignores it.
#define APIARY_EXPOSE APIARY_DETAIL_ANNOTATE("expose")

// Suppress binding for a declaration that would otherwise be exposed
// (e.g. an inherited member from a base flagged APIARY_EXPOSE).
#define APIARY_HIDE APIARY_DETAIL_ANNOTATE("hide")

// ---------------------------------------------------------------------------
// Naming and placement
// ---------------------------------------------------------------------------

// Override the Python identifier used for the binding.
#define APIARY_RENAME(py_name) APIARY_DETAIL_ANNOTATE("rename:" py_name)

// Place the binding inside a Python submodule (e.g. "tensor", "linalg").
#define APIARY_MODULE(submodule) APIARY_DETAIL_ANNOTATE("module:" submodule)

// ---------------------------------------------------------------------------
// Class options
// ---------------------------------------------------------------------------

// Override the pybind11 holder type. Default is std::unique_ptr.
//     APIARY_HOLDER(std::shared_ptr)
#define APIARY_HOLDER(...) APIARY_DETAIL_ANNOTATE("holder:" #__VA_ARGS__)

// Enable the pybind11 buffer protocol for this class (required for NumPy
// zero-copy interop on tensor types). Pair with APIARY_BUFFER_FROM
// to actually emit the ``.def_buffer()`` call — buffer_protocol alone
// only flips the class option.
#define APIARY_BUFFER_PROTOCOL APIARY_DETAIL_ANNOTATE("buffer_protocol")

// Designate a free function (or static member) that takes the bound type
// and returns ``py::buffer_info`` describing its memory layout. The
// codegen wraps the call in a ``.def_buffer([](T &self) { return
// helper(self); })`` lambda. The helper is your responsibility — it has
// to know the concrete instantiation's element type, rank, and stride
// representation, and produce a valid buffer_info.
//
//     namespace einsums::pybuf {
//         template <typename T, size_t N, typename A>
//         pybind11::buffer_info make(GeneralTensor<T, N, A> &t);
//     }
//
//     APIARY_BUFFER_PROTOCOL
//     APIARY_BUFFER_FROM(einsums::pybuf::make)
//     struct GeneralTensor { ... };
#define APIARY_BUFFER_FROM(...) APIARY_DETAIL_ANNOTATE("buffer_from:" #__VA_ARGS__)

// ---------------------------------------------------------------------------
// Plan C protocols — codegen-emitted Python protocol bindings (buffer,
// iterator, subscript) from pure-C++ helpers + neutral types in
// Einsums/Python/Protocol.hpp. The directive carries every metadatum the
// codegen needs to emit the entire backend-specific lambda; user-side
// helpers see only neutral types in their signatures.
// ---------------------------------------------------------------------------

// Standard-layout buffer protocol. The codegen emits a per-backend
// .def_buffer (pybind11) / __dlpack__ (nanobind) lambda that builds the
// backend-native buffer description from the named member functions on
// the bound class.
//
// All four method names are required. ``element_type`` names the class
// template parameter that gives the element type; the codegen substitutes
// it per instantiation when emitting the format string and sizeof().
//
//   template <typename T, typename Alloc>
//   APIARY_EXPOSE
//   APIARY_BUFFER_PROTOCOL_STD(
//       data = data, rank = rank, dim = dim, stride = stride,
//       element_type = T)
//   struct GeneralRuntimeTensor { ... };
//
// The named methods must satisfy:
//   - ``T*       data();``     // pointer to first element
//   - ``size_t   rank() const;``
//   - ``size_t   dim(int) const;``     // dim along an axis
//   - ``size_t   stride(int) const;``  // stride in **element units**
//
// (Stride conversion to byte units happens inside the codegen lambda.)
#define APIARY_BUFFER_PROTOCOL_STD(...) APIARY_DETAIL_ANNOTATE("buffer_protocol_std:" #__VA_ARGS__)

// Standard iterator protocol. The codegen emits a per-backend
// __iter__ binding that returns a Python iterator over the half-open
// range [begin(), end()). User-side methods stay STL-style; no
// pybind11 in their signatures.
//
// All values name member functions that satisfy the standard
// LegacyForwardIterator (or stronger) requirements:
//   - ``Iter begin();``
//   - ``Iter end();``
//
//   template <typename T, typename Alloc>
//   APIARY_EXPOSE
//   APIARY_ITERATOR_STD(begin = begin, end = end)
//   struct GeneralRuntimeTensor {
//       Iter begin();   // pure C++
//       Iter end();
//   };
//
// The Python iterator borrows a reference to the underlying object
// (py::keep_alive<0, 1>) so iteration is safe across the parent's
// lifetime.
#define APIARY_ITERATOR_STD(...) APIARY_DETAIL_ANNOTATE("iterator_std:" #__VA_ARGS__)

// Standard index protocol — scalar reads/writes via __getitem__/__setitem__.
// The codegen emits per-backend lambdas dispatching on Python type
// (py::int_, py::tuple), normalizing negative indices, bounds-checking,
// and calling the user's pure-C++ helpers.
//
//   element_type — the class template parameter naming the scalar type
//   rank         — member function returning the tensor's runtime rank
//   dim          — member function (size_t i) → size_t (axis size)
//   at_element   — T   (std::vector<int64_t> const &idx) const
//   set_element  — void(std::vector<int64_t> const &idx, T value)
//
//   template <typename T, typename Alloc>
//   APIARY_EXPOSE
//   APIARY_INDEX_PROTOCOL_STD(
//       element_type = T, rank = rank, dim = dim,
//       at_element = at_element, set_element = set_element)
//   struct GeneralRuntimeTensor {
//       T    at_element(std::vector<int64_t> const &) const;
//       void set_element(std::vector<int64_t> const &, T);
//   };
//
// Python:
//   t[5]            → scalar (rank-1 tensor)
//   t[0, 1]         → scalar (full int-indexing)
//   t[5] = 1.0      → write scalar
//   t[0, 1] = 2.0   → write scalar
//
// Negative indices ("t[-1]") are normalized to positive against the dim
// before being passed to user helpers. Out-of-range raises py::index_error.
//
// Slice/partial-index reads (returning a view) and buffer bulk-assign are
// not yet emitted — they need APIARY_INDEX_PROTOCOL_STD's
// extended form (with view_type and at_view) to land alongside the
// RuntimeTensorView binding. Until then, slice access throws a clear
// "view return not yet bound" error.
#define APIARY_INDEX_PROTOCOL_STD(...) APIARY_DETAIL_ANNOTATE("index_protocol_std:" #__VA_ARGS__)

// Register a one-way implicit conversion from ``Source`` to the
// annotated class. Emits ``py::implicitly_convertible<Source, Class>()``
// after the class definition. Useful for ergonomic factory ctors:
//
//     APIARY_EXPOSE
//     APIARY_IMPLICIT_FROM(int)
//     class Tensor { Tensor(int n); ... };  // can call Python f(t=42)
#define APIARY_IMPLICIT_FROM(...) APIARY_DETAIL_ANNOTATE("implicit_from:" #__VA_ARGS__)

// Allow Python instances to carry arbitrary attributes (py::dynamic_attr()).
#define APIARY_DYNAMIC_ATTR APIARY_DETAIL_ANNOTATE("dynamic_attr")

// Skip generation of the copy constructor binding.
#define APIARY_NOCOPY APIARY_DETAIL_ANNOTATE("nocopy")

// Skip generation of the move constructor binding.
#define APIARY_NOMOVE APIARY_DETAIL_ANNOTATE("nomove")

// Bind a class field as read-only (``def_readonly``). Without this, fields
// marked APIARY_EXPOSE bind read-write.
#define APIARY_READONLY APIARY_DETAIL_ANNOTATE("readonly")

// Mark a class as a Python exception type rather than a regular bound
// class. The codegen emits ``py::register_exception<T>(m, "name")``
// instead of ``py::class_<T>(...)``. The C++ class must derive from
// ``std::exception`` (or whatever pybind11's register_exception template
// requires) for the registration to compile.
//
//     class APIARY_EXPOSE
//           APIARY_EXCEPTION
//           TensorError : public std::exception { ... };
//
// Subsequent ``raise einsums.TensorError(...)`` from Python (or
// ``throw einsums::TensorError("...")`` from C++) crosses the boundary
// cleanly. Nanobind has a different exception API; this directive is
// pybind11-specific and is silently dropped under the nanobind target.
#define APIARY_EXCEPTION APIARY_DETAIL_ANNOTATE("exception")

// Force-skip emission of base-class arguments to ``py::class_<>``.
//
// Usually unnecessary: by default the emitter only forwards bases that
// are themselves bound in this run, so unbound internal bases are
// silently dropped. Use this directive only when you want to explicitly
// hide a bound base from the Python class hierarchy.
#define APIARY_NO_BASES APIARY_DETAIL_ANNOTATE("no_bases")

// ---------------------------------------------------------------------------
// Method and free-function options
// ---------------------------------------------------------------------------

// Override return_value_policy. Argument is the unqualified policy name.
//     APIARY_RVP(reference_internal)
#define APIARY_RVP(policy) APIARY_DETAIL_ANNOTATE("rvp:" #policy)

// Emit py::keep_alive<nurse, patient>() for this call.
//     APIARY_KEEP_ALIVE(0, 1)   // return value keeps arg 1 alive
#define APIARY_KEEP_ALIVE(nurse, patient) APIARY_DETAIL_ANNOTATE("keep_alive:" #nurse ":" #patient)

// Wrap the call in py::call_guard<py::gil_scoped_release>().
#define APIARY_RELEASE_GIL APIARY_DETAIL_ANNOTATE("release_gil")

// Variadic-pack expansion for templated method/ctor binding. Indicates
// that the (assumed-last) parameter is a parameter pack, and that the
// expanded arity is given by the template parameter named ``param_name``.
// Each expanded slot has type ``element_type``.
//
//   template <typename T, size_t rank> struct Tensor;
//
//   template <typename... Dims>
//   APIARY_EXPOSE
//   APIARY_VARIADIC_FROM(rank, size_t)
//   Tensor(std::string name, Dims... dims);
//
// For ``Tensor<double, 2>``, this binds a ctor with signature
// ``(std::string, size_t, size_t)``.
#define APIARY_VARIADIC_FROM(param_name, element_type)                                                                             \
    APIARY_DETAIL_ANNOTATE("variadic_from:" #param_name ":" #element_type)

// Pin a member-template parameter to a concrete type for binding purposes.
// pybind11 cannot bind a member template directly — you must specify which
// instantiation to expose. Apply this directive on a method or constructor
// that itself has template parameters (distinct from the enclosing class's
// template parameters); the codegen substitutes the binding into the
// emitted signature and into static_cast<>'d method-pointer expressions.
//
// Multiple member-template parameters: stack the directive once per
// parameter. Position-independent — the codegen matches by name.
//
//   template <typename T, typename Alloc>
//   struct GeneralRuntimeTensor {
//     template <Container Dim>
//     APIARY_EXPOSE
//     APIARY_INSTANTIATE_MEMBER(Dim = std::vector<size_t>)
//     GeneralRuntimeTensor(std::string name, Dim const &dims);
//   };
//
// For ``GeneralRuntimeTensor<float, std::allocator<float>>`` this binds
// a constructor with signature ``(std::string, std::vector<size_t> const &)``.
#define APIARY_INSTANTIATE_MEMBER(...) APIARY_DETAIL_ANNOTATE("instantiate_member:" #__VA_ARGS__)

// Multi-instantiation form of APIARY_INSTANTIATE_MEMBER. Each
// directive defines one Python binding for a templated method; multiple
// directives stack to fan a method out across dtypes / ranks (mirror of
// APIARY_INSTANTIATE_AS for free functions).
//
// The first argument is the Python name. Subsequent arguments are
// ``Param = Type`` assignments pinning each member-template parameter to
// a concrete type for this instantiation.
//
//   template <typename T, typename Alloc = std::allocator<T>>
//   APIARY_EXPOSE
//   APIARY_INSTANTIATE_MEMBER_AS("declare_runtime_tensor",
//                                        T = float,  Alloc = std::allocator<float>)
//   APIARY_INSTANTIATE_MEMBER_AS("declare_runtime_tensor",
//                                        T = double, Alloc = std::allocator<double>)
//   GeneralRuntimeTensor<T, Alloc> &declare_runtime_tensor(...);
//
// Sharing a Python name across directives produces a pybind11 overload
// set; py_name disambiguation is the user's responsibility.
#define APIARY_INSTANTIATE_MEMBER_AS(py_name, ...) APIARY_DETAIL_ANNOTATE("instantiate_member_as:" py_name ":" #__VA_ARGS__)

// Bind this method as a Python operator (e.g. "__add__", "__matmul__")
// instead of an ordinary named function.
#define APIARY_OPERATOR(py_name) APIARY_DETAIL_ANNOTATE("operator:" py_name)

// ---------------------------------------------------------------------------
// Properties
// ---------------------------------------------------------------------------

// Bind a method as the getter of a Python property.
#define APIARY_GETTER(py_name) APIARY_DETAIL_ANNOTATE("getter:" py_name)

// Bind a method as the setter of a Python property. Pair with a matching
// APIARY_GETTER on the read accessor.
#define APIARY_SETTER(py_name) APIARY_DETAIL_ANNOTATE("setter:" py_name)

// ---------------------------------------------------------------------------
// Documentation
// ---------------------------------------------------------------------------

// Override the docstring extracted from the doxygen comment block above
// this declaration.
#define APIARY_DOC(text) APIARY_DETAIL_ANNOTATE("doc:" text)

// ---------------------------------------------------------------------------
// Template instantiation
// ---------------------------------------------------------------------------
//
// The cross-product directives (INSTANTIATE / INSTANTIATE_TEMPLATE) require
// each parameter list to be tagged with the *exact* C++ template-parameter
// name from the class declaration.  The codegen tool matches by name, not
// by position, so order in the macro is free.  Anything that doesn't match
// a real template parameter is rejected with a clear diagnostic — this is
// how we guard against random macro expansion (e.g. some upstream header
// having ``#define Element WHATEVER``) silently mangling the payload.

// Cross-product template instantiation.
//
// Each ``Param(values...)`` group's keyword must match one of the C++
// template-parameter names; the codegen tool errors out on mismatch,
// duplicates, missing parameters, or arity mismatches. Python identifiers
// for the resulting bindings are auto-derived (``Tensor_float_2`` etc.);
// use ``APIARY_INSTANTIATE_TEMPLATE`` if you want to control the
// name explicitly.
//
//   template <typename T, size_t rank>
//   APIARY_INSTANTIATE(Tensor,
//       T(float, double, std::complex<float>, std::complex<double>),
//       rank(1, 2, 3, 4))
//   struct Tensor;
//
// Reordering is allowed; this is equivalent to the above:
//
//   APIARY_INSTANTIATE(Tensor,
//       rank(1, 2, 3, 4),
//       T(float, double, std::complex<float>, std::complex<double>))
#define APIARY_INSTANTIATE(...) APIARY_DETAIL_ANNOTATE("instantiate:" #__VA_ARGS__)

// Single-instance instantiation with an explicit Python-side name. Use
// this when:
//   - You want a specific Python identifier that isn't a function of the
//     template arguments;
//   - Or one template parameter depends on another (e.g. ``Alloc`` =
//     ``std::allocator<T>``), which a flat cross-product can't express.
//
// The Python name comes first so the C++ type, whose template arguments
// may contain commas, can occupy the variadic tail.
//
//   APIARY_INSTANTIATE_AS("Tensor2d_double",
//                                 GeneralTensor<double, 2, std::allocator<double>>)
#define APIARY_INSTANTIATE_AS(py_name, ...) APIARY_DETAIL_ANNOTATE("instantiate_as:" py_name ":" #__VA_ARGS__)

// Cross-product instantiation with a Python-name template. Same matching
// rules as ``APIARY_INSTANTIATE``: each parameter list keyword
// must be a C++ template-parameter name. Placeholders in the name
// template use those same names.
//
//   template <typename Element, int Rank> class Block;
//
//   APIARY_INSTANTIATE_TEMPLATE("Block_{Element}_{Rank}",
//       Block,
//       Element(float, double),
//       Rank(1, 2))
//
// Produces ``Block_float_1``, ``Block_float_2``, ``Block_double_1``,
// ``Block_double_2``. Placeholder values are sanitized to valid Python
// identifiers (``std::complex<double>`` -> ``std_complex_double``).
#define APIARY_INSTANTIATE_TEMPLATE(name_template, ...)                                                                            \
    APIARY_DETAIL_ANNOTATE("instantiate_template:" name_template ":" #__VA_ARGS__)

// Declare Python kwarg names for the leading bool template parameters of a
// templated free function. The count of names tells the codegen how many
// leading template params are bool flags; their values become Python
// keyword-only arguments with the supplied names. Required when using
// ``APIARY_INSTANTIATE_BOOLS``.
//
//   template <bool TransA, bool TransB, MatrixConcept T, typename U>
//   APIARY_EXPOSE
//   APIARY_TEMPLATE_KWARGS("trans_a", "trans_b")
//   APIARY_INSTANTIATE_BOOLS("gemm", einsums::GeneralRuntimeTensor<float, std::allocator<float>>, float)
//   void gemm(U const alpha, T const &A, T const &B, U const beta, T *C);
//
// The Python signature becomes
// ``gemm(alpha, A, B, beta, C, *, trans_a=False, trans_b=False)`` and
// dispatches at runtime to the right ``gemm<TA, TB, T, U>`` instantiation.
#define APIARY_TEMPLATE_KWARGS(...) APIARY_DETAIL_ANNOTATE("template_kwargs:" #__VA_ARGS__)

// Cross-product instantiation over the leading bool template parameters.
// Requires ``APIARY_TEMPLATE_KWARGS`` to declare how many leading
// template params are bool flags. Expands to 2^N
// ``APIARY_INSTANTIATE_AS`` entries with every false/true
// combination prepended in lexicographic order
// ((false,false), (false,true), (true,false), (true,true) for N=2).
//
// The variadic tail is the comma-separated *non-bool* template arguments
// for one dtype slice — same syntax as ``APIARY_INSTANTIATE_AS``
// minus the leading bool prefix. Repeat the directive once per dtype.
//
//   APIARY_TEMPLATE_KWARGS("trans_a", "trans_b")
//   APIARY_INSTANTIATE_BOOLS("gemm", einsums::GeneralRuntimeTensor<float,  std::allocator<float>>,  float)
//   APIARY_INSTANTIATE_BOOLS("gemm", einsums::GeneralRuntimeTensor<double, std::allocator<double>>, double)
//   ...
//
// Use ``APIARY_INSTANTIATE_AS`` directly if you need to omit a
// particular bool combination for a given dtype (rare escape hatch).
#define APIARY_INSTANTIATE_BOOLS(py_name, ...) APIARY_DETAIL_ANNOTATE("instantiate_bools:" py_name ":" #__VA_ARGS__)
