# ----------------------------------------------------------------------------------------------
# Copyright (c) The Einsums Developers. All rights reserved.
# Licensed under the MIT License. See LICENSE.txt in the project root for license information.
# ----------------------------------------------------------------------------------------------
"""Fixture top-level package for the static Python frontend (apiary_py_extract.py).

This is the pure-Python shell a consumer adds on top of the compiled extension.
Nothing here is imported or executed by apiary — it is parsed statically.
"""

from .linalg import Decomposition, solve  # re-export into the top-level namespace

# __all__ is the authoritative public surface. ``solve``/``Decomposition`` are
# re-exports (aliases) — already documented at their definition site in
# einsums.linalg, so they must NOT be re-emitted here as einsums.solve /
# einsums.Decomposition. ``version`` is defined locally and IS documented.
# ``debug_dump`` is defined locally but absent from __all__, so it is private.
__all__ = ["version", "solve", "Decomposition"]


def version() -> str:
    """Return the package version string.

    A trivial top-level free function, used to check that ``einsums/__init__.py``
    content lands on the top-level ``einsums`` page (submodule == top).
    """
    return "0.0.0-fixture"


def debug_dump() -> None:
    """A public-looking helper that is NOT in ``__all__``, so it is undocumented."""
    raise NotImplementedError
