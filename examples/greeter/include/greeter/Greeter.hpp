//----------------------------------------------------------------------------------------------
// Copyright (c) The Einsums Developers. All rights reserved.
// Licensed under the MIT License. See LICENSE.txt in the project root for license information.
//----------------------------------------------------------------------------------------------

#pragma once

#include <apiary/Annotations.hpp>

#include <cctype>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

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

    /// Greet with a custom word, made emphatic when ``emphatic`` is true.
    APIARY_EXPOSE Greeter(std::string greeting, bool emphatic) : _greeting(std::move(greeting)) {
        if (emphatic) {
            _greeting += "!!";
        }
    }

    /// Return ``"<greeting>, <name>!"``. Raises GreetingError when ``name`` is empty.
    APIARY_EXPOSE std::string say(std::string const &name) const {
        if (name.empty()) {
            throw GreetingError("nobody to greet");
        }
        return _greeting + ", " + name + "!";
    }

    /// ``say(name)``, in capitals when ``loud`` is true.
    APIARY_EXPOSE std::string say_aloud(std::string const &name, bool loud) const {
        std::string out = say(name);
        if (loud) {
            for (char &c : out) {
                c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            }
        }
        return out;
    }

    /// The greeting word, exposed as a read/write Python property ``greeting``.
    APIARY_GETTER("greeting") std::string const &greeting() const { return _greeting; }
    APIARY_SETTER("greeting") void set_greeting(std::string value) { _greeting = std::move(value); }

  private:
    std::string _greeting;
};

/// A free function — bound as a module-level function ``shout``.
/// Two greeters taking turns. Owns both, and hands them out by reference.
class APIARY_EXPOSE Conversation {
  public:
    APIARY_EXPOSE Conversation() : _opener("Hello"), _closer("Goodbye") {}

    /// Both speakers, as references into this conversation: changing one
    /// changes the conversation, and each keeps the conversation alive.
    APIARY_EXPOSE APIARY_RVP(reference_internal) std::tuple<Greeter &, Greeter &> speakers() { return {_opener, _closer}; }

    /// Both speakers again, through const pointers.
    APIARY_EXPOSE APIARY_RVP(reference_internal) std::pair<Greeter const *, Greeter const *> speakers_view() const {
        return {&_opener, &_closer};
    }

    /// What the two say to ``name``, one after the other.
    APIARY_EXPOSE std::string exchange(std::string const &name) const { return _opener.say(name) + " " + _closer.say(name); }

  private:
    Greeter _opener;
    Greeter _closer;
};

/// Who ``speaker`` is, or ``"nobody"`` when there is no speaker (None).
APIARY_EXPOSE inline std::string introduce(Greeter const *speaker) {
    return speaker == nullptr ? std::string{"nobody"} : speaker->greeting();
}

/// A greeting for ``name``, or for the room when there is no name (None).
APIARY_EXPOSE inline std::string welcome(std::optional<std::string> const &name) {
    return Greeter("Welcome").say(name.value_or("everyone"));
}

/// Greet each of ``names`` that ``keep`` accepts, one greeting per line.
APIARY_EXPOSE inline std::string greet_some(std::vector<std::string> const &names, std::function<bool(std::string const &)> const &keep) {
    Greeter const hello;
    std::string   out;
    for (std::string const &name : names) {
        if (keep(name)) {
            out += hello.say(name) + "\n";
        }
    }
    return out;
}

APIARY_EXPOSE inline std::string shout(std::string const &text) {
    std::string out = text;
    for (char &c : out) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return out + "!";
}

} // namespace greeter
