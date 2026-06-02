//----------------------------------------------------------------------------------------------
// Copyright (c) The Einsums Developers. All rights reserved.
// Licensed under the MIT License. See LICENSE.txt in the project root for license information.
//----------------------------------------------------------------------------------------------

#pragma once

// Post-IR pass that collapses @getter / @setter directive pairs on a
// class's methods into a flat list of BoundProperty entries on the
// class. Mirrors the merge the pybind11 C++ emitter does inline; both
// emitters can consume the precomputed view, but the .pyi emitter is
// the primary consumer.
//
// Rules (matching emit_method's @getter logic):
//
//   * A method tagged @getter("X") becomes the getter for property X.
//   * A sibling method tagged @setter("X") (same X) becomes the setter.
//   * No matching setter → read-only property.
//   * Property docstring comes from the getter's doc.
//
// Recurses into nested classes.

#include "IR.hpp"

namespace apiary {

/// @brief Collapse @getter / @setter directive pairs on a class's methods into BoundProperty entries, recursing into nested classes.
/// @param cls The bound class whose methods are scanned for property directives.
void compute_properties(BoundClass &cls);

/// @brief Compute properties for every class in a module.
/// @param module_ The module whose classes are processed.
void compute_properties(Module &module_);

} // namespace apiary
