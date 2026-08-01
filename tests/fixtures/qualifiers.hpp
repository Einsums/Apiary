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
// It used not to (Finding 7): all four emitted one of two casts between them -
// `(...)() const` and `(...)()` - and clang rejected both with "address of
// overloaded function 'value' cannot be static_cast to type ...". The generated
// TU did not compile.
//
// The fix is two-sided, which is why it took a decision rather than a repair.
// `&` and `const &` just need the qualifier in the cast. `&&` and `const &&`
// cannot be bound through a pointer-to-member AT ALL - pybind11 invokes it on
// the lvalue it holds - so they are bound through a lambda that casts self to
// an rvalue. That is what `&&` means, and it is also why the emitter reports
// it: after such a call the Python object is in a moved-from state and nothing
// in Python will indicate that. The alternative was to refuse to bind them,
// which honours the safety and ignores the APIARY_EXPOSE the author wrote.
//
// The two-name shape below is what makes the lambda defensible here: an author
// who gives the rvalue overload its own Python name (`take`) has said they want
// the moving call. One who does not gets the lvalue overload under the natural
// name and a warning about the other.

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
