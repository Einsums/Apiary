//----------------------------------------------------------------------------------------------
// Copyright (c) The Einsums Developers. All rights reserved.
// Licensed under the MIT License. See LICENSE.txt in the project root for license information.
//----------------------------------------------------------------------------------------------

// Fixture: APIARY_EXCEPTION. An exception class binds as
// py::register_exception<T> rather than a py::class_, and stubs as a Python
// Exception subclass. pybind11's translator needs only what(), so the classes
// here declare it themselves and stay free of the standard library the golden
// runs do not have.

#pragma once

#include <apiary/Annotations.hpp>

namespace einsums::fixture {

/// Raised when input cannot be parsed.
class APIARY_EXPOSE APIARY_EXCEPTION ParseError {
  public:
    char const *what() const noexcept { return "parse error"; }
};

/// Raised when a limit is exceeded. Renamed, and registered in a submodule.
class APIARY_EXPOSE APIARY_EXCEPTION APIARY_RENAME("LimitExceeded") APIARY_MODULE("limits") LimitError {
  public:
    char const *what() const noexcept { return "limit exceeded"; }
};

/// Parse a value, throwing ParseError when it is negative.
APIARY_EXPOSE inline int parse(int value) {
    if (value < 0) {
        throw ParseError{};
    }
    return value;
}

} // namespace einsums::fixture
