//----------------------------------------------------------------------------------------------
// Copyright (c) The Einsums Developers. All rights reserved.
// Licensed under the MIT License. See LICENSE.txt in the project root for license information.
//----------------------------------------------------------------------------------------------

// Cross-module fixture: a class hierarchy whose members do not all live in the
// same Python submodule, and entities with degenerate documentation.
//
// inheritance.hpp covers the docs graph's relationship edges within one
// module. What was never covered is the BINDING side of a hierarchy that
// straddles a submodule boundary: pybind11 needs the base's py::class_ to
// already exist when the derived class names it as a base, and the two are now
// registered against different module objects. Ordering across submodules is a
// different problem from ordering within one.
//
// The docless entities at the end are here because "no doc comment" and "doc
// comment that is nothing but an @rst block" are the two shapes where every
// docstring-handling path has to cope with an empty or wholly-substituted
// result, and both were untested.

#pragma once

#include <apiary/Annotations.hpp>

namespace einsums::fixture {

/// A base bound into the ``core`` submodule.
class APIARY_EXPOSE APIARY_MODULE("core") Shape {
  public:
    APIARY_EXPOSE Shape();

    /// Area of the shape.
    APIARY_EXPOSE virtual double area() const;
};

/// A derived class bound into a DIFFERENT submodule from its base. The base's
/// binding has to be emitted first even though it belongs to another module
/// object.
class APIARY_EXPOSE APIARY_MODULE("shapes") Circle : public Shape {
  public:
    APIARY_EXPOSE Circle();

    /// Area of the circle.
    APIARY_EXPOSE double area() const override;
};

/// A free function in a third submodule taking the base by reference and
/// returning the derived type, so both names must resolve from a module that
/// declares neither.
APIARY_EXPOSE APIARY_MODULE("util") Circle enclosing_circle(Shape const &shape);

// An entity with NO doc comment at all. Everything downstream has to produce
// an absent docstring rather than an empty one, and the stub must not emit a
// stray empty docstring line.
APIARY_EXPOSE double undocumented(double x);

// KNOWN BAD (Finding 11): the binding emitter uses the RAW doc text as the
// Python docstring, never doc_structured, so every Doxygen command survives
// into help(). Here that means the `@rst` / `@endrst` markers and the
// directive's indentation both land in the docstring verbatim. It is not
// specific to @rst - `@brief`, `@param` and `@return` leak the same way, in
// the shipped Einsums bindings, which is where this was confirmed at runtime.
// Fixing it is a format decision (what SHOULD a docstring composed from
// brief/detail/params/returns look like?) and it rewrites every docstring
// golden, so it is not done here.
/**
 * @rst
 * Documented only through an embedded reST block, with no prose of its own.
 * The whole doc comment is one protected span, so the brief is produced
 * entirely by splicing rather than by any of the line handling.
 *
 * .. note::
 *
 *    Including a directive, to prove the splice survives indentation.
 * @endrst
 */
APIARY_EXPOSE double rst_only(double x);

} // namespace einsums::fixture
