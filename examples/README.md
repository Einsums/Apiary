# Apiary examples

Self-contained projects that consume Apiary the way a downstream library
would — via `find_package(Apiary)` and the `apiary_*` CMake helpers, with no
Einsums coupling.

| Example | Shows |
|---------|-------|
| [`greeter`](greeter/) | The C++ binding pipeline on a header-only library: `apiary_detect_toolchain` → `apiary_add_bindings` → `apiary_aggregate_extension`. Common annotations (class, rename, method, getter/setter property, free function). |
| [`mathx`](mathx/) | The full **documentation** pipeline across **two source languages**: a C++ core + a hand-written Python layer, extracted by both frontends and merged into one cross-linked reference with curation (`## Topics`), per-type rubrics, articles, availability badges, and `apiary_add_python_docs`. |

Each example has its own `README.md` with build/run instructions. They expect
an installed Apiary on `CMAKE_PREFIX_PATH` (or swap the `find_package` for an
`add_subdirectory` of the Apiary tree).
