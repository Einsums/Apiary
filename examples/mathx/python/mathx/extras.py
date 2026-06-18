# ----------------------------------------------------------------------------------------------
# Copyright (c) The Einsums Developers. All rights reserved.
# Licensed under the MIT License. See LICENSE.txt in the project root for license information.
# ----------------------------------------------------------------------------------------------
"""Hand-written convenience helpers layered on the bound :py:class:`Vec2`.

These live on the ``mathx.extras`` page; they are pure Python, so they are seen
only by Apiary's static ``ast`` frontend — never by the C++ binding pass.
"""

from __future__ import annotations

from typing import deprecated

# apiary's static frontend ignores imports (it only walks def/class), so this
# runtime import of the bound type does not create a duplicate symbol.
from ._core import Vec2


def normalize(v: Vec2) -> Vec2:
    """Return ``v`` scaled to unit length.

    Computed from [[Vec2/length]] — a cross-language link: the annotation and
    the ``[[ ]]`` reference resolve to the C++-bound ``Vec2`` on its own page.

    Parameters
    ----------
    v
        The vector to normalize.

    Returns
    -------
        A new unit-length vector parallel to ``v``.

    .. versionadded:: 1.0.0
    """
    n = v.length()
    return Vec2(v.x / n, v.y / n)


def lerp(a: Vec2, b: Vec2, *, t: float = 0.5) -> Vec2:
    """Linearly interpolate between ``a`` and ``b``.

    Parameters
    ----------
    a
        Start vector (``t == 0``).
    b
        End vector (``t == 1``).
    t
        Interpolation parameter in ``[0, 1]``.
    """
    return Vec2(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t)


@deprecated("Use normalize() instead.")
def unit(v: Vec2) -> Vec2:
    """Deprecated alias for [[normalize]].

    .. deprecated:: 1.2.0
    """
    return normalize(v)
