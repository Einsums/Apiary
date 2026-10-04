//----------------------------------------------------------------------------------------------
// Copyright (c) The Einsums Developers. All rights reserved.
// Licensed under the MIT License. See LICENSE.txt in the project root for license information.
//----------------------------------------------------------------------------------------------

// A C interface, written the way C headers are: every declaration inside
// ``extern "C"``, an opaque handle typedef'd to its own struct, and a callback
// as a function-pointer typedef.

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/// An opaque handle to a collection of shapes.
typedef struct geom_shapes geom_shapes; /* NOLINT(modernize-use-using): a C header */

/// Called once for each shape a visit reaches.
typedef void (*geom_visit_fn)(void *user, geom_shapes *shapes, int index); /* NOLINT(modernize-use-using) */

/// Call @p fn with @p user for every shape in @p shapes.
void geom_visit(geom_shapes *shapes, geom_visit_fn fn, void *user);

/// Scale every shape in @p shapes by @p factor: the C counterpart of geom::scale,
/// whose page label must differ from this one's.
void geom_scale(geom_shapes *shapes, int factor);

#ifdef __cplusplus
}
#endif
