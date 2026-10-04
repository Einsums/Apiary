//----------------------------------------------------------------------------------------------
// Copyright (c) The Einsums Developers. All rights reserved.
// Licensed under the MIT License. See LICENSE.txt in the project root for license information.
//----------------------------------------------------------------------------------------------

#include <demo/Modules.hpp>

#include <pybind11/pybind11.h>

PYBIND11_MODULE(_core, m) {
    demo_register_all(m);
}
