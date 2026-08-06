//----------------------------------------------------------------------------------------------
// Copyright (c) The Einsums Developers. All rights reserved.
// Licensed under the MIT License. See LICENSE.txt in the project root for license information.
//----------------------------------------------------------------------------------------------

// Fixture: an ABI-versioning inline namespace, the shape a library takes on
// when it wraps itself in `namespace lib { inline namespace vN { ... } }` so
// two copies in one process cannot interpose on each other.
//
// The inline namespace is transparent to name lookup, so nothing here should
// reach the emitted bindings, the stubs, or the docs JSON's names. What it
// used to reach was `symbol_id`, whose Clang USR records every enclosing
// DeclContext: bumping the ABI tag renumbered every ID in the documentation.
// This fixture is what keeps that fixed.

#pragma once

#include <apiary/Annotations.hpp>

#define FIXTURE_ABI_TAG v7

namespace einsums {
inline namespace FIXTURE_ABI_TAG {
namespace fixture {

/// An enum inside the versioned namespace.
enum class APIARY_EXPOSE Layout { Row, Col };

/// A base class, for the inheritance edge.
class APIARY_EXPOSE Shape {
  public:
    /// Constructor.
    Shape();

    /// A virtual method, for the override edge.
    virtual int rank() const;
};

/// A derived class, so `inheritsFrom` and `overrides` edges are exercised.
class APIARY_EXPOSE Widget : public Shape {
  public:
    /// Constructor.
    Widget();

    /// Overrides Shape::rank.
    int rank() const override;
};

/// Takes and returns types from the same inline namespace.
APIARY_EXPOSE Widget make(Layout l = Layout::Row);

/// A plain function, whose USR has no nested types in it at all.
APIARY_EXPOSE int add(int a, int b);

} // namespace fixture
} // namespace FIXTURE_ABI_TAG
} // namespace einsums
