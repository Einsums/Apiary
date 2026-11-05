# Apiary examples

Self-contained projects that consume Apiary the way a downstream library
would — via `find_package(Apiary)` and the `apiary_*` CMake helpers, with no
Einsums coupling.

| Example | Shows |
|---------|-------|
| [`greeter`](greeter/) | The C++ binding pipeline on a header-only library: `apiary_detect_toolchain` → `apiary_add_bindings` → `apiary_aggregate_extension`. Common annotations (class, rename, method, getter/setter property, free function). |
| [`mathx`](mathx/) | The full **documentation** pipeline across **two source languages**: a C++ core + a hand-written Python layer, extracted by both frontends and merged into one cross-linked reference with curation (`## Topics`), per-type rubrics, articles, availability badges, and `apiary_add_python_docs`. |

| [`grid`](grid/) | Python's **container protocols** from plain C++: `APIARY_BUFFER_PROTOCOL_STD` (memoryview / NumPy, zero copy), `APIARY_INDEX_PROTOCOL_STD` (integer, tuple, slice and `...` indexing, returning views), `APIARY_ITERATOR_STD`, and an `APIARY_EXCEPTION`. |

Each example has its own `README.md` with build/run instructions. They expect
an installed Apiary on `CMAKE_PREFIX_PATH` (or swap the `find_package` for an
`add_subdirectory` of the Apiary tree).

Every example builds against pybind11 by default or nanobind with
`-DAPIARY_BACKEND=nanobind`, from the same sources; its test script passes
unchanged under both. Adding `-DAPIARY_STABLE_ABI=ON` to a nanobind build
makes each one stable-ABI module for every Python from 3.12 on.
