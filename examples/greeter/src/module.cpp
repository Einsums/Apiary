//----------------------------------------------------------------------------------------------
// Copyright (c) The Einsums Developers. All rights reserved.
// Licensed under the MIT License. See LICENSE.txt in the project root for license information.
//----------------------------------------------------------------------------------------------

// The PYBIND11_MODULE body. apiary_aggregate_extension() generates the
// <greeter/Modules.hpp> header (declaring greeter_register_all() + each
// module's register function); we just include it and call the aggregator.
#include <greeter/Modules.hpp>

#include <pybind11/pybind11.h>

PYBIND11_MODULE(greeter, m) {
    m.doc() = "Apiary example extension";
    greeter_register_all(m);
}
