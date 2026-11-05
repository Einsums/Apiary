#!/usr/bin/env python3
"""Smoke test for the Apiary greeter example.

Run after building:  PYTHONPATH=build python3 test_greeter.py
"""
import gc
import sys

import greeter

g = greeter.Greeter()
assert g.say("world") == "Hello, world!", g.say("world")

g2 = greeter.Greeter("Hi")
assert g2.say("Apiary") == "Hi, Apiary!"

# Read/write property built from the getter/setter pair.
assert g2.greeting == "Hi"
g2.greeting = "Hey"
assert g2.say("you") == "Hey, you!"

# Free function.
assert greeter.shout("loud") == "LOUD!"

# A C++ exception marked APIARY_EXCEPTION arrives as its own Python type.
assert issubclass(greeter.GreetingError, Exception)
try:
    g.say("")
except greeter.GreetingError as e:
    assert str(e) == "nobody to greet", str(e)
else:
    raise AssertionError("say('') did not raise GreetingError")

# None reaches a pointer parameter as nullptr and an optional one as nullopt.
assert greeter.introduce(g2) == "Hey"
assert greeter.introduce(None) == "nobody"
assert greeter.welcome("Ada") == "Welcome, Ada!"
assert greeter.welcome(None) == "Welcome, everyone!"

# A bool parameter, a constructor's bool and a predicate's return all accept
# what pybind11's bool accepts: True and False, anything with __bool__ (an int,
# None, numpy.bool_), and nothing else - a list has no __bool__ slot.
class Truthy:
    def __init__(self, truth):
        self.truth = truth

    def __bool__(self):
        return self.truth


for yes in (True, 1, Truthy(True)):
    assert g.say_aloud("ada", yes) == "HELLO, ADA!", (yes, g.say_aloud("ada", yes))
for no in (False, 0, None, Truthy(False)):
    assert g.say_aloud("ada", no) == "Hello, ada!", (no, g.say_aloud("ada", no))
expect_type_error = False
try:
    g.say_aloud("ada", [1])
except TypeError:
    expect_type_error = True
assert expect_type_error, "a list was accepted as a bool"
assert greeter.Greeter("Yo", 1).say("x") == "Yo!!, x!"
names = ["ada", "bob", "cy"]
assert greeter.greet_some(names, lambda n: n != "bob") == "Hello, ada!\nHello, cy!\n"
assert greeter.greet_some(names, lambda n: len(n)) == "Hello, ada!\nHello, bob!\nHello, cy!\n"
assert greeter.greet_some(names, lambda n: Truthy(n == "cy")) == "Hello, cy!\n"
try:
    import numpy as np
except ImportError:
    np = None
if np is not None:
    assert g.say_aloud("ada", np.bool_(True)) == "HELLO, ADA!"
    assert greeter.greet_some(names, lambda n: np.asarray(len(n)) > 2) == "Hello, ada!\nHello, bob!\n"

# A tuple of references hands back the conversation's own speakers, not copies:
# a change made through one shows up in the conversation.
conv = greeter.Conversation()
opener, closer = conv.speakers()
assert conv.exchange("you") == "Hello, you! Goodbye, you!", conv.exchange("you")
opener.greeting = "Hi"
assert conv.exchange("you") == "Hi, you! Goodbye, you!", conv.exchange("you")

# The same through a pair of const pointers.
seen_opener, seen_closer = conv.speakers_view()
assert (seen_opener.greeting, seen_closer.greeting) == ("Hi", "Goodbye")

# Each speaker keeps its conversation alive (reference_internal), so dropping
# the last Python reference to the conversation leaves both usable.
del conv, seen_opener, seen_closer
gc.collect()
assert opener.say("x") == "Hi, x!" and closer.say("x") == "Goodbye, x!"

# greeter._extras is written by hand against <apiary/Python.hpp>: the same source
# under either library.
from greeter import _extras

assert _extras.backend in ("pybind11", "nanobind"), _extras.backend
assert _extras.call(lambda n: n.upper(), "ada") == "ADA"


def refuse(name):
    raise ValueError("not " + name)


reported = _extras.call(refuse, "ada")
assert reported.startswith("raised: ") and "not ada" in reported, reported
assert _extras.platform() == sys.platform, _extras.platform()

print("greeter example: OK")
