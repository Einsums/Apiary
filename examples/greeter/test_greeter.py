#!/usr/bin/env python3
"""Smoke test for the Apiary greeter example.

Run after building:  PYTHONPATH=build python3 test_greeter.py
"""
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

print("greeter example: OK")
