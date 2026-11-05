#!/usr/bin/env python3
"""Behavior test for the Apiary grid example, identical under every backend.

Run after building:  PYTHONPATH=build python3 test_grid.py
"""
import gc
import struct
import sys

import grid


def expect_raises(exc, fn, *args):
    try:
        fn(*args)
    except exc as e:
        return e
    raise AssertionError(f"{fn} did not raise {exc.__name__}")


# --- index protocol -------------------------------------------------------
g = grid.GridD([2, 3])
assert g.rank() == 2 and g.dim(0) == 2 and g.dim(1) == 3 and g.size() == 6
g[0, 1] = 5.0
g[-1, -1] = 7.0  # negative indices count from the end
g[1, 0] = 2  # an int converts to the element type
assert g[0, 1] == 5.0 and g[1, 2] == 7.0 and g[1, 0] == 2.0
expect_raises(IndexError, lambda: g[2, 0])
expect_raises(IndexError, lambda: g[0, -4])

# --- iterator protocol ----------------------------------------------------
assert list(g) == [0.0, 5.0, 0.0, 2.0, 0.0, 7.0], list(g)
it = iter(grid.GridD([2]))  # the iterator keeps its grid alive
gc.collect()
assert list(it) == [0.0, 0.0]

# --- buffer protocol: shape, format, and zero copy -------------------------
mv = memoryview(g)
assert mv.ndim == 2 and mv.shape == (2, 3) and mv.format == "d", (mv.ndim, mv.shape, mv.format)
assert mv.tolist() == [[0.0, 5.0, 0.0], [2.0, 0.0, 7.0]], mv.tolist()
mv[1, 1] = 9.0  # a write through the buffer lands in the C++ storage
assert g[1, 1] == 9.0
del mv

# The exported memory outlives the Python object it came from.
mv = memoryview(grid.GridD([4]))
gc.collect()
assert mv.tolist() == [0.0, 0.0, 0.0, 0.0]
del mv

# --- views through the index protocol -------------------------------------
row = g[1]  # an int on a rank-2 grid collapses axis 0
assert memoryview(row).tolist() == [2.0, 9.0, 7.0]
col = g[:, 2]  # a mixed tuple: full on axis 0, index on axis 1
assert memoryview(col).tolist() == [0.0, 7.0], memoryview(col).tolist()
assert col.stride(0) == 3
assert memoryview(g[0:2:1]).shape == (2, 3)
assert memoryview(g[...]).tolist() == memoryview(g).tolist()
rev = g[::-1]  # a negative step walks backwards through the same storage
assert memoryview(rev).tolist() == [[2.0, 9.0, 7.0], [0.0, 5.0, 0.0]], memoryview(rev).tolist()
del g
gc.collect()
assert memoryview(row).tolist() == [2.0, 9.0, 7.0]  # a view keeps the storage alive

# --- complex elements -----------------------------------------------------
z = grid.GridZ([2])
z[0] = 1 + 2j
z[1] = 3  # real numbers convert too
assert z[0] == 1 + 2j and z[1] == 3 + 0j
assert memoryview(z).format == "Zd", memoryview(z).format
# memoryview cannot unpack "Zd" itself (tolist() raises NotImplementedError),
# so read the bytes: two (real, imag) pairs of doubles.
assert struct.unpack("4d", memoryview(z).cast("B")) == (1.0, 2.0, 3.0, 0.0)

# --- C++ exceptions -------------------------------------------------------
assert issubclass(grid.GridError, Exception)
e = expect_raises(grid.GridError, row.at, [0, 0])  # row is rank 1
assert str(e) == "expected 1 indices, got 2", str(e)
assert row.at([2]) == 7.0

# A buffer export that the C++ side refuses is a BufferError whose __cause__ is
# the C++ exception, translated as any other call would translate it.
gone = grid.GridD([2])
gone.release()
e = expect_raises(BufferError, memoryview, gone)
assert isinstance(e.__cause__, grid.GridError), repr(e.__cause__)
assert str(e.__cause__) == "grid has been released", str(e.__cause__)

# --- numpy, when it is installed ------------------------------------------
try:
    import numpy as np
except ImportError:
    np = None
if np is not None:
    h = grid.GridD([2, 2])
    a = np.asarray(h)
    assert a.shape == (2, 2) and a.dtype == np.float64
    a[0, 1] = 4.0
    assert h[0, 1] == 4.0  # numpy aliases the grid's memory
    assert np.array_equal(np.asarray(h[:, 1]), np.array([4.0, 0.0]))
    assert np.asarray(z).dtype == np.complex128 and np.asarray(z).tolist() == [1 + 2j, 3 + 0j]

print(f"grid example ({'numpy' if np is not None else 'no numpy'}, python {sys.version_info[0]}.{sys.version_info[1]}): OK")
