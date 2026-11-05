//----------------------------------------------------------------------------------------------
// Copyright (c) The Einsums Developers. All rights reserved.
// Licensed under the MIT License. See LICENSE.txt in the project root for license information.
//----------------------------------------------------------------------------------------------

// greeter._extras: a module written by hand rather than generated, built by
// apiary_add_extension_module with whichever library APIARY_BACKEND names.
// <apiary/Python.hpp> names that library, so this file never does.

#include <apiary/Python.hpp>

#include <string>

namespace py = apiary::python::py;

APIARY_EXTENSION_MODULE(_extras, m) {
    m.doc() = "Hand-written beside the generated greeter._core.";

    m.attr("backend") = apiary::python::backend_name;

    // Call back into Python from C++, and report what it raised rather than
    // letting the exception through.
    m.def(
        "call",
        [](py::object const &fn, std::string const &name) -> std::string {
            try {
                return py::cast<std::string>(fn(name));
            } catch (apiary::python::PythonError const &e) {
                return std::string("raised: ") + e.what();
            }
        },
        py::arg("fn"), py::arg("name"), "fn(name), or what it raised.");

    m.def(
        "platform", []() { return py::cast<std::string>(apiary::python::import_module("sys").attr("platform")); },
        "sys.platform, read through an import from C++.");
}
