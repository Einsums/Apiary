//----------------------------------------------------------------------------------------------
// Copyright (c) The Einsums Developers. All rights reserved.
// Licensed under the MIT License. See LICENSE.txt in the project root for license information.
//----------------------------------------------------------------------------------------------

/// @file
/// The Python-binding library an extension module is built with, for code written by hand.
///
/// apiary generates bindings for whichever library APIARY_BACKEND names. Code outside them - an
/// extension's module body, a library header that calls back into Python, a hand-written module
/// - includes this header instead of naming a library, and is then the same source under either.
///
/// It names what the two libraries spell differently, and nothing else:
///
/// - `apiary::python::py`, the library's namespace, for the API both spell the same way:
///   `py::object`, `py::dict`, `py::list`, `py::str`, `py::cast<T>()`, `py::hasattr`, `py::arg`,
///   `py::gil_scoped_acquire`, `py::gil_scoped_release`, `module_::def`, `object::attr`.
/// - `apiary::python::PythonError`, the exception a call into Python throws when Python raised.
/// - `apiary::python::import_module()`, which the two spell `import` and `import_`.
/// - `apiary::python::backend_name`, "pybind11" or "nanobind".
/// - `APIARY_EXTENSION_MODULE(name, m)`, the module-definition block.
///
/// Where the libraries behave differently under the same spelling, write the form that is right
/// under both. The one most easily missed: converting an attribute to a typed handle with
/// `py::dict d = obj.attr("x")` is checked by pybind11 and an unchecked borrow under nanobind,
/// so convert with `py::cast<py::dict>(...)`, which checks under both.
///
/// The library comes from APIARY_BACKEND_PYBIND11 or APIARY_BACKEND_NANOBIND, which
/// apiary_aggregate_extension and apiary_add_extension_module define on the module they create.
/// The header pulls in the library itself and its std::string and std::vector casters; include
/// any other caster a signature needs as usual.

#pragma once

#if defined(APIARY_BACKEND_NANOBIND) && defined(APIARY_BACKEND_PYBIND11)
#    error "<apiary/Python.hpp>: both APIARY_BACKEND_NANOBIND and APIARY_BACKEND_PYBIND11 are defined"
#elif defined(APIARY_BACKEND_NANOBIND)
#    include <nanobind/nanobind.h>
#    include <nanobind/stl/string.h>
#    include <nanobind/stl/vector.h>
#elif defined(APIARY_BACKEND_PYBIND11)
#    include <pybind11/pybind11.h>
#    include <pybind11/stl.h>
#else
#    error "<apiary/Python.hpp> needs APIARY_BACKEND_PYBIND11 or APIARY_BACKEND_NANOBIND; create the module with apiary_aggregate_extension or apiary_add_extension_module, which define it"
#endif

namespace apiary::python {

#if defined(APIARY_BACKEND_NANOBIND)
namespace py = ::nanobind;

/// The exception a call into Python throws when the Python side raised.
using PythonError = ::nanobind::python_error;

/// Name of the binding library, as APIARY_BACKEND spells it.
inline constexpr char const *backend_name = "nanobind";

/// Import a Python module by its dotted name.
inline py::module_ import_module(char const *name) {
    return py::module_::import_(name);
}
#else
namespace py = ::pybind11;

/// The exception a call into Python throws when the Python side raised.
using PythonError = ::pybind11::error_already_set;

/// Name of the binding library, as APIARY_BACKEND spells it.
inline constexpr char const *backend_name = "pybind11";

/// Import a Python module by its dotted name.
inline py::module_ import_module(char const *name) {
    return py::module_::import(name);
}
#endif

} // namespace apiary::python

// The same definition the register header apiary_aggregate_extension generates gives, so either
// may come first.
#ifndef APIARY_EXTENSION_MODULE
#    if defined(APIARY_BACKEND_NANOBIND)
#        define APIARY_EXTENSION_MODULE(name, variable) NB_MODULE(name, variable)
#    else
#        define APIARY_EXTENSION_MODULE(name, variable) PYBIND11_MODULE(name, variable)
#    endif
#endif
