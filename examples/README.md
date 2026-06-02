# Apiary examples

Self-contained projects that consume Apiary the way a downstream library
would — via `find_package(Apiary)` and the `apiary_*` CMake helpers, with no
Einsums coupling.

| Example | Shows |
|---------|-------|
| [`greeter`](greeter/) | The full pipeline on a header-only library: `apiary_detect_toolchain` → `apiary_add_bindings` → `apiary_aggregate_extension`. Common annotations (class, rename, method, getter/setter property, free function). |

Each example has its own `README.md` with build/run instructions. They expect
an installed Apiary on `CMAKE_PREFIX_PATH` (or swap the `find_package` for an
`add_subdirectory` of the Apiary tree).
