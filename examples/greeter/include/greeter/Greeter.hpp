//----------------------------------------------------------------------------------------------
// Copyright (c) The Einsums Developers. All rights reserved.
// Licensed under the MIT License. See LICENSE.txt in the project root for license information.
//----------------------------------------------------------------------------------------------

#pragma once

#include <apiary/Annotations.hpp>

#include <cctype>
#include <stdexcept>
#include <string>
#include <utility>

namespace greeter {

/// Raised by ``Greeter.say`` when there is nobody to greet.
///
/// ``APIARY_EXCEPTION`` binds it as a Python exception type: C++ code throws
/// it, and Python catches ``greeter.GreetingError``.
class APIARY_EXPOSE APIARY_EXCEPTION GreetingError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

/// A friendly greeter — the obligatory binding-tool "hello world".
///
/// Demonstrates the common annotations: a renamed, non-copyable class with
/// constructors, a method, and a read/write property built from a
/// getter/setter pair.
class APIARY_EXPOSE APIARY_RENAME("Greeter") Greeter {
  public:
    /// Greet with the default word ("Hello").
    APIARY_EXPOSE Greeter() : _greeting("Hello") {}

    /// Greet with a custom word.
    APIARY_EXPOSE explicit Greeter(std::string greeting) : _greeting(std::move(greeting)) {}

    /// Return ``"<greeting>, <name>!"``. Raises GreetingError when ``name`` is empty.
    APIARY_EXPOSE std::string say(std::string const &name) const {
        if (name.empty()) {
            throw GreetingError("nobody to greet");
        }
        return _greeting + ", " + name + "!";
    }

    /// The greeting word, exposed as a read/write Python property ``greeting``.
    APIARY_GETTER("greeting") std::string const &greeting() const { return _greeting; }
    APIARY_SETTER("greeting") void set_greeting(std::string value) { _greeting = std::move(value); }

  private:
    std::string _greeting;
};

/// A free function — bound as a module-level function ``shout``.
APIARY_EXPOSE inline std::string shout(std::string const &text) {
    std::string out = text;
    for (char &c : out) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return out + "!";
}

} // namespace greeter
