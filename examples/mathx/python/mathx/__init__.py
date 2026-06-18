# ----------------------------------------------------------------------------------------------
# Copyright (c) The Einsums Developers. All rights reserved.
# Licensed under the MIT License. See LICENSE.txt in the project root for license information.
# ----------------------------------------------------------------------------------------------
"""mathx — a tiny 2-D vector library.

The compiled core (:py:class:`Vec2`, :py:func:`dot`) is bound from C++ by
Apiary; this package adds a hand-written convenience layer on top. Apiary
documents BOTH — the C++ surface via its Clang frontend and this pure-Python
shell via the static ``ast`` frontend — and merges them into one reference.
"""

from ._core import Vec2, dot          # re-exported from the compiled extension
from .extras import lerp, normalize

# ``__all__`` is the public surface. ``Vec2`` / ``dot`` are re-exports (already
# documented on the core), and ``normalize`` / ``lerp`` live in mathx.extras, so
# none are re-documented here; only ``version`` is defined and documented here.
__all__ = ["Vec2", "dot", "normalize", "lerp", "version"]


def version() -> str:
    """Return the mathx version string."""
    return "1.0.0"
