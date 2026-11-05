# grid: container protocols from plain C++

`include/grid/Grid.hpp` declares `Grid<T>`, a dense array of any rank, and `GridView<T>`, a strided view into one.
Neither includes pybind11 or nanobind.
Three directives name ordinary member functions, and Apiary writes the Python protocol code for whichever binding library the build selects:

| Directive | Python |
|-----------|--------|
| `APIARY_BUFFER_PROTOCOL_STD` | `memoryview(g)`, `numpy.asarray(g)`: zero copy, writable, and the memory outlives the object |
| `APIARY_INDEX_PROTOCOL_STD` | `g[0, 1]`, `g[-1, -1] = 7`, and views from `g[1]`, `g[1:3]`, `g[:, 0]`, `g[::-1]`, `g[...]` |
| `APIARY_ITERATOR_STD` | `for x in g`, `list(g)` |

`APIARY_EXCEPTION` makes `grid::GridError` arrive in Python as `grid.GridError`.

The index protocol hands slicing to `Grid::view` as a list of `grid::SliceSpec`, named by the directive's `slice_spec` key.

## Build and run

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=<apiary-install-prefix>
cmake --build build
PYTHONPATH=build python3 test_grid.py
```

Add `-DAPIARY_BACKEND=nanobind` to build against nanobind instead; `apiary_find_backend()` locates it through the Python it builds for.
`test_grid.py` is the same for both, and so is the behavior it checks.
