# greeter — minimal Apiary example

Binds a small header-only C++ library (`include/greeter/Greeter.hpp`) into a
Python extension named `greeter`, using Apiary's CMake helpers end to end:
`apiary_detect_toolchain` → `apiary_add_bindings` → `apiary_aggregate_extension`.

It exercises the common annotations: a renamed class with constructors, a
method, a getter/setter property, and a free function (see the `APIARY_*`
markers in the header).

## Build & run

Apiary must be discoverable by `find_package(Apiary)`. Either install it and
point `CMAKE_PREFIX_PATH` at the prefix:

```bash
# from a built Apiary tree:  cmake --install <apiary-build> --prefix /tmp/apiary
cmake -S . -B build -DCMAKE_PREFIX_PATH=/tmp/apiary
cmake --build build
PYTHONPATH=build python3 test_greeter.py        # -> "greeter example: OK"
```

…or vendor Apiary instead of installing it by replacing the `find_package`
call in `CMakeLists.txt` with `add_subdirectory(<path-to-apiary> apiary)`.

## What gets generated

Under `build/gen/`:

- `greeter_core_pybind.cpp` — the pybind11 binding TU.
- `greeter_core.pyi` — the type stub fragment.
- `greeter_core.docs.json` — a structured description of the bound surface.
- `include/greeter/Modules.hpp` — the `greeter_register_all()` aggregator the
  `src/module.cpp` `PYBIND11_MODULE` body includes.

## Scaling to multiple modules

Call `apiary_add_bindings` once per module (each with its own
`REGISTER_FUNCTION` and `OUT_BINDING`), then pass all the module names and
generated TUs to a single `apiary_aggregate_extension` — its generated
`<prefix>all()` registers every module under one `import`.
