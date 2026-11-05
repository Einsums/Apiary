//----------------------------------------------------------------------------------------------
// Copyright (c) The Einsums Developers. All rights reserved.
// Licensed under the MIT License. See LICENSE.txt in the project root for license information.
//----------------------------------------------------------------------------------------------

// The extension's module body. apiary_aggregate_extension() generates the
// <greeter/Modules.hpp> header (declaring greeter_register_all() + each
// module's register function, and APIARY_EXTENSION_MODULE, which is
// PYBIND11_MODULE or NB_MODULE for the configured backend); we just include it
// and call the aggregator.
#include <greeter/Modules.hpp>

APIARY_EXTENSION_MODULE(_core, m) {
    m.doc() = "Apiary example extension";
    greeter_register_all(m);
}
