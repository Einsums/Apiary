# ----------------------------------------------------------------------------------------------
# Copyright (c) The Einsums Developers. All rights reserved.
# Licensed under the MIT License. See LICENSE.txt in the project root for license information.
# ----------------------------------------------------------------------------------------------
"""Each dispatched name in the fast_dispatch fixture against its ordered-search twin.

Run by run_fast_dispatch.sh with the directory holding the built extension.
"""

import sys

sys.path.insert(0, sys.argv[1])

import fixture_fast_dispatch as m  # noqa: E402


def box(cls, value):
    b = cls()
    b.value = value
    return b


def outcome(fn, *args, **kwargs):
    """What a call did: its result, or its error with the function's own name taken out."""
    try:
        return ("ok", fn(*args, **kwargs))
    except TypeError as e:
        return ("TypeError", str(e).replace(fn.__name__, "<name>").split("Invoked with")[0])


# fill: every instantiation, positionally and by keyword, with the default and with a value
# that needs a conversion.
for cls in (m.BoxI, m.BoxL, m.BoxF, m.BoxD):
    for fn in (m.fill, m.fill_slow):
        outs = [cls() for _ in range(5)]
        fn(outs[0], box(cls, 3))
        fn(out=outs[1], **{"in": box(cls, 3)})
        fn(outs[2], box(cls, 3), 1)
        fn(outs[3], box(cls, 3), scale=True)
        fn(outs[4], box(cls, 3), 0.5)
        values = [o.value for o in outs]
        expected = [6, 6, 3, 3, int(1.5) if cls in (m.BoxI, m.BoxL) else 1.5]
        assert values == expected, (fn.__name__, cls.__name__, values)

# Calls the dispatcher cannot take go to the ordered search, and fail or succeed exactly as it does.
for args, kwargs in [
    ((m.BoxI(), m.BoxD()), {}),  # instantiations mixed
    ((m.BoxD(),), {}),  # an argument missing
    ((m.BoxD(), m.BoxD()), {"bogus": 1}),  # an unknown keyword
    ((m.BoxD(), m.BoxD(), "x"), {}),  # a non-key argument that does not convert
    ((m.BoxD(), m.BoxD(), 1.0, 2.0), {}),  # too many arguments
    ((m.BoxD(), m.BoxD()), {"out": m.BoxD()}),  # one argument given twice
    ((None, m.BoxD()), {}),  # None for a pointer: valid, and not a key type
    ((m.Scaled(), m.BoxD()), {}),  # a subclass: not an exact key type
]:
    fast, slow = outcome(m.fill, *args, **kwargs), outcome(m.fill_slow, *args, **kwargs)
    assert fast == slow, (args, kwargs, fast, slow)

# The docstring is the overload set's own: the slow twin's, up to the name and the doc text.
FILL_DOC = "Write ``in`` times ``scale`` into ``out``."
SLOW_DOC = "fill, kept on pybind11's ordered search: the reference the dispatcher's behaviour is checked against."
assert m.fill.__doc__ == m.fill_slow.__doc__.replace(SLOW_DOC, FILL_DOC).replace("fill_slow", "fill"), m.fill.__doc__
assert not [n for n in dir(m) if "apiary" in n], dir(m)

# A subclass given to an overload set reaches the first overload whose casters accept it, which is
# the base's when the base's overload comes first: the dispatcher must not pick the exact type.
for name in ("which_base_first", "which_derived_first"):
    fast, slow = getattr(m, name), getattr(m, name + "_slow")
    for cls in (m.BoxI, m.BoxL, m.BoxD, m.Scaled):
        for s in (1.0, 1, True):
            for call in (lambda f: f(cls(), s), lambda f: f(box=cls(), s=s), lambda f: f(cls(), s=s)):
                assert call(fast) == call(slow), (name, cls.__name__, s, call(fast), call(slow))
assert m.which_base_first(m.Scaled(), 1.0) == m.which_base_first(m.BoxD(), 1.0)
assert m.which_derived_first(m.Scaled(), 1.0) != m.which_derived_first(m.BoxD(), 1.0)

# A scalar the codegen keyed as a class keys nothing at run time: given or omitted, by position or
# keyword, as int or float, every call matches the ordered search.
for cls in (m.BoxI, m.BoxL, m.BoxF, m.BoxD):
    for args, kwargs in [((box(cls, 2),), {}), ((box(cls, 2), 0.5), {}), ((box(cls, 2), 3), {}), ((box(cls, 2),), {"w": 4.0}),
                         ((), {"out": box(cls, 2)}), ((box(cls, 2), "x"), {})]:
        fast, slow = outcome(m.weigh, *args, **kwargs), outcome(m.weigh_slow, *args, **kwargs)
        assert fast == slow, (cls.__name__, args, kwargs, fast, slow)

print("fast dispatch: OK")
