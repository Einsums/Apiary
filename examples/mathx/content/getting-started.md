# Getting Started

``mathx`` is a worked example of Apiary documenting **two source languages** at
once: an annotated C++ core and a hand-written Python layer, merged into one
reference.

## Install

Build the extension and put it on the import path, then:

    import mathx

    v = mathx.Vec2(3.0, 4.0)
    assert v.length() == 5.0

## Where things live

- [[Vec2]] and [[dot]] are bound from C++ (``include/mathx/Vec.hpp``).
- [[normalize]] and [[lerp]] are hand-written Python (``mathx.extras``).

Every link above resolves across the language boundary — that is the whole
point of the merged docs graph.
