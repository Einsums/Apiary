#!/usr/bin/env python3
"""Smoke test for the Apiary mathx example.

Run after building:  PYTHONPATH=build python3 test_mathx.py
"""
import mathx

# Bound C++ core.
v = mathx.Vec2(3.0, 4.0)
assert v.length() == 5.0, v.length()

# Read/write properties from the getter/setter pairs.
v.x = 6.0
assert (v.x, v.y) == (6.0, 4.0)

# Free function bound from C++.
assert mathx.dot(mathx.Vec2(1.0, 0.0), mathx.Vec2(1.0, 2.0)) == 1.0

# Hand-written Python convenience layer (mathx.extras), re-exported at top level.
u = mathx.normalize(mathx.Vec2(0.0, 2.0))
assert (u.x, u.y) == (0.0, 1.0), (u.x, u.y)

m = mathx.lerp(mathx.Vec2(0.0, 0.0), mathx.Vec2(2.0, 4.0), t=0.5)
assert (m.x, m.y) == (1.0, 2.0), (m.x, m.y)

assert mathx.version() == "1.0.0"

print("mathx example: OK")
