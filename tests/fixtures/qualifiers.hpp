//----------------------------------------------------------------------------------------------
// Copyright (c) The Einsums Developers. All rights reserved.
// Licensed under the MIT License. See LICENSE.txt in the project root for license information.
//----------------------------------------------------------------------------------------------

// Method-qualifier fixture: cv- and ref-qualified overloads.
//
// 6ca313e fixed ref-qualifiers being dropped from the rendered C++ signature
// and left nothing pinning the behavior, so the next person to touch signature
// rendering has no way to know they broke it. This fixture is that pin.
//
// The interesting property is that the qualifier is part of the OVERLOAD's
// identity: `value() const &` and `value() &&` are two C++ functions, so the
// disambiguating static_cast the emitter writes has to carry the qualifier or
// it names neither of them.
//
// KNOWN BAD (Finding 7): it does not. The four overloads below emit two
// distinct casts between them - `(...)() const` and `(...)()` - and clang
// rejects both with "address of overloaded function 'value' cannot be
// static_cast to type ...", because a ref-qualified member function's type
// includes its ref-qualifier. The generated TU does not compile. Verified by
// compiling the emitted cast against the declarations.
//
// Note that the fix is not only "put the qualifier in the cast". pybind11
// invokes through the member pointer on an LVALUE, so `&` and `const &` work
// once the cast is right, but `&&` and `const &&` cannot be bound through a
// member pointer at all - they need a lambda that moves, or a refusal. That is
// a decision about what binding an rvalue-qualified method should MEAN in
// Python, not a mechanical repair, so the golden records the broken output
// until it is made.

#pragma once

#include <apiary/Annotations.hpp>

namespace einsums::fixture {

/// A handle whose accessors differ only in cv- and ref-qualification.
class APIARY_EXPOSE Handle {
  public:
    APIARY_EXPOSE Handle();

    /// Read the value from a const lvalue.
    APIARY_EXPOSE APIARY_RENAME("value") double value() const &;

    /// Read the value from a non-const lvalue.
    APIARY_EXPOSE APIARY_RENAME("value_mut") double value() &;

    /// Steal the value from an rvalue.
    APIARY_EXPOSE APIARY_RENAME("take") double value() &&;

    /// Steal the value from a const rvalue - the rarest of the four, and the
    /// one a hand-written qualifier check is most likely to miss.
    APIARY_EXPOSE APIARY_RENAME("take_const") double value() const &&;

    /// An unqualified overload set for contrast: these differ by parameter,
    /// not by qualifier, and must stay a normal overload set.
    APIARY_EXPOSE void reset();
    /// Reset to an explicit value.
    APIARY_EXPOSE void reset(double v);

    /// A noexcept const method, to prove noexcept rides along with const.
    [[nodiscard]] APIARY_EXPOSE bool empty() const noexcept;
};

} // namespace einsums::fixture
