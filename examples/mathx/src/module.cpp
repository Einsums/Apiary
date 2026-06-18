//----------------------------------------------------------------------------------------------
// Copyright (c) The Einsums Developers. All rights reserved.
// Licensed under the MIT License. See LICENSE.txt in the project root for license information.
//----------------------------------------------------------------------------------------------

// The compiled core extension, imported by the package as ``mathx._core``.
// apiary_aggregate_extension() generates <mathx/Modules.hpp> (declaring
// mathx_register_all() + each module's register function); we include it and
// call the aggregator from the PYBIND11_MODULE body.
#include <mathx/Modules.hpp>

#include <pybind11/pybind11.h>

PYBIND11_MODULE(_core, m) {
    m.doc() = "mathx compiled core (bound from C++ by Apiary)";
    mathx_register_all(m);
}
