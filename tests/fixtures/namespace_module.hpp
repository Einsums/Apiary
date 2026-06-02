//----------------------------------------------------------------------------------------------
// Copyright (c) The Einsums Developers. All rights reserved.
// Licensed under the MIT License. See LICENSE.txt in the project root for license information.
//----------------------------------------------------------------------------------------------

// Phase C.13 fixture: APIARY_MODULE on a namespace declaration is
// inherited by every annotated entity inside the namespace. Per-entity
// overrides win.

#pragma once

#include <apiary/Annotations.hpp>

namespace einsums::fixture {

namespace APIARY_MODULE("graph") graph {

/// Inherits the enclosing namespace's module — binds into ``graph``.
APIARY_EXPOSE int inherited(int x);

/// Per-entity override binds into ``graph.ops`` instead.
APIARY_EXPOSE APIARY_MODULE("graph.ops") int overridden(int x);

} // namespace APIARY_MODULE("graph")graph

/// Outside the annotated namespace — binds at the top level.
APIARY_EXPOSE int top_level(int x);

} // namespace einsums::fixture
