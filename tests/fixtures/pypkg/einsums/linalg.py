# ----------------------------------------------------------------------------------------------
# Copyright (c) The Einsums Developers. All rights reserved.
# Licensed under the MIT License. See LICENSE.txt in the project root for license information.
# ----------------------------------------------------------------------------------------------
"""Hand-written convenience layer over the ``einsums.linalg`` bound surface.

Used as the static-Python-frontend fixture: documented free functions and a
documented class with a method. Lands on the ``einsums.linalg`` submodule page,
alongside any C++-origin ``einsums.linalg`` symbols.
"""

from __future__ import annotations

from typing import overload


@overload
def norm(x: Vector) -> float: ...
@overload
def norm(x: Matrix, ord: int) -> float: ...
def norm(x, ord=2):
    """Compute a vector or matrix norm.

    The implementation docstring documents the whole ``@overload`` set.

    Parameters
    ----------
    x
        The array to measure.
    ord
        The order of the norm.
    """
    raise NotImplementedError


def solve(a, b, *, assume_a: str = "gen"):
    """Solve the linear system ``a @ x == b``.

    A pure-Python convenience wrapper. The signature exercises positional args
    (unannotated) and an annotated keyword-only argument with a default.

    Parameters
    ----------
    a
        Coefficient matrix.
    b
        Right-hand side.
    assume_a
        Structure assumption for ``a``.

    Returns
    -------
        The solution ``x``.
    """
    return _backend_solve(a, b, assume_a)


def cho_factor(matrix: ArrayLike) -> Decomposition:
    """Cholesky-factor ``matrix``.

    Exercises annotation recovery: the parameter is annotated with an
    undocumented name (collapses to ``Any`` in the rendered page) and the return
    is a documented class (cross-links to its page).
    """
    return Decomposition()


def _backend_solve(a, b, assume_a):
    """Private helper — must NOT appear in the documented surface."""
    raise NotImplementedError


class Decomposition:
    """A lazily-evaluated matrix decomposition.

    A hand-written helper class layered on the bound tensor types.
    """

    def __init__(self, kind: str = "lu"):
        """Create an empty decomposition of the given ``kind``."""
        self._kind = kind

    def factor(self, matrix: ArrayLike, *factors, pivoting: bool = True) -> Decomposition:
        """Compute and cache the decomposition of ``matrix``.

        Parameters
        ----------
        matrix
            The matrix to decompose.
        pivoting
            Whether to use partial pivoting.
        """
        return self

    @property
    def rank(self) -> int:
        """The numerical rank of the factored matrix."""
        return 0

    @rank.setter
    def rank(self, value: int) -> None:
        self._rank = value

    @staticmethod
    def identity(n: int) -> Decomposition:
        """Return the decomposition of the ``n``-by-``n`` identity."""
        return Decomposition()

    @classmethod
    def from_kind(cls, kind: str) -> Decomposition:
        """Construct a decomposition selecting the algorithm by ``kind``."""
        return cls(kind)

    def _private_helper(self):
        """Must NOT be documented."""
        return None


class LUDecomposition(Decomposition):
    """An LU decomposition (exercises a Python-origin inheritsFrom edge)."""

    def solve_with(self, b: ArrayLike) -> ArrayLike:
        """Solve the system using the cached factors."""
        raise NotImplementedError
