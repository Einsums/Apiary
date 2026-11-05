# Apiary

A libclang/libtooling-based C++ introspection engine. It parses real C++23 with
a Clang frontend and, from a single AST walk, emits pybind11/nanobind binding
TUs, `.pyi` type stubs, Sphinx C++ API documentation (cpp/c-domain directives,
including macros — no Doxygen), and a structured public-API JSON IR.

Apiary began as `einsums-pybind`, the binding generator for the
[Einsums](https://github.com/Einsums/Einsums) tensor library, and is now a
standalone tool. The annotation contract lives in
`include/apiary/Annotations.hpp` (the `APIARY_*` macros).

Einsums remains the **reference consumer**: its `einsums_add_module(... PYBIND)`
helper is a thin wrapper over the generic `apiary_add_bindings` /
`apiary_aggregate_extension` functions ([Quick start](#quick-start)), driving
them to aggregate its modules under a single `import einsums`. New consumers
call those functions directly — there is no Einsums coupling.

## Quick start

Annotate the C++ you want to bind:

```cpp
// mylib/include/mylib/Greeter.hpp
#include <apiary/Annotations.hpp>

namespace mylib {

class APIARY_EXPOSE Greeter {
public:
    APIARY_EXPOSE Greeter();
    APIARY_EXPOSE explicit Greeter(std::string greeting);
    APIARY_EXPOSE std::string say(std::string const &name) const;
};

} // namespace mylib
```

Wire it up with Apiary's CMake helpers — the same ones whether Apiary is
installed (`find_package`) or vendored (`add_subdirectory`):

```cmake
find_package(Apiary REQUIRED COMPONENTS tool)   # or: add_subdirectory(external/apiary)

target_link_libraries(mylib PUBLIC apiary::annotations)   # the APIARY_* header

# Probe the build compiler once for the system/stdlib include paths libtooling
# needs (sets APIARY_SYSTEM_FLAGS).
apiary_detect_toolchain(CXX_STANDARD 20)

# Generate the binding TU (+ .pyi + docs JSON). DEPENDS_TARGETS supplies the
# transitive -I/-D from your library's usage requirements.
apiary_add_bindings(
    BINDING DOCS_JSON
    MODULE            mylib     # the Python module the stubs and docs JSON name
    HEADERS           ${CMAKE_SOURCE_DIR}/mylib/include/mylib/Greeter.hpp
    SOURCE_INCLUDES   mylib/Greeter.hpp
    REGISTER_FUNCTION apiary_register_mylib
    DEPENDS_TARGETS   mylib
    OUTPUT_NAME       mylib
    CXX_STANDARD      20
    OUT_BINDING _tu  OUT_STUB _stub  OUT_DOCS_JSON _docs
)

# Assemble one or more modules into a single Python extension. You supply MAIN
# (the module body); the helper generates the register header it includes and
# creates the pybind11 target (or nanobind's: see "Backend target").
apiary_aggregate_extension(
    NAME _core
    MAIN     ${CMAKE_SOURCE_DIR}/mylib/src/main.cpp
    BINDINGS ${_tu}
    MODULES  mylib
    MODULES_HEADER      ${CMAKE_BINARY_DIR}/gen/include/mylib/Modules.hpp
    MODULES_INCLUDE_DIR ${CMAKE_BINARY_DIR}/gen/include
    STUBS ${_stub}  STUBS_TARGET mylib_stubs
    FRAG_DIR ${CMAKE_BINARY_DIR}/gen  PKG_DIR ${CMAKE_BINARY_DIR}/mylib
)
target_link_libraries(_core PRIVATE mylib)
```

`main.cpp` includes the generated header and calls the aggregator:

```cpp
#include <mylib/Modules.hpp>           // generated: declares apiary_register_all()
APIARY_EXTENSION_MODULE(_core, m) { apiary_register_all(m); }   // PYBIND11_MODULE or NB_MODULE
```

Build and import — the codegen runs as a build edge (re-fires on header changes):

```bash
cmake -S . -B build && cmake --build build
PYTHONPATH=build python3 -c "import _core; print(_core.Greeter('hi').say('world'))"
```

That's the whole consumer surface — `apiary_detect_toolchain`,
`apiary_add_bindings`, `apiary_aggregate_extension`. Call `apiary_add_bindings`
once per module and pass them all to a single `apiary_aggregate_extension` to
aggregate many modules under one extension.

## Annotation reference

Every macro is a C++11 attribute that places between the class-key and
the class name (or before a function return type). Multiple macros
stack. Under non-Clang compilers all macros expand to nothing, so
production builds carry no overhead.

### Exposure

| Macro | Purpose |
|---|---|
| `APIARY_EXPOSE` | Mark a declaration for binding. Without this, the codegen ignores it. |
| `APIARY_HIDE` | Suppress binding for an otherwise-exposed declaration (e.g. an inherited member). |

### Naming

| Macro | Purpose |
|---|---|
| `APIARY_RENAME("py_name")` | Override the Python identifier used for the binding. |
| `APIARY_MODULE("submodule")` | Place the binding inside a Python submodule. Dotted names (`"tensor.algebra"`) request nested submodules. |
| `APIARY_EXCEPTION` | Bind the class as a Python exception via `py::register_exception<T>` instead of `py::class_<>`. C++ class must derive from `std::exception` (or compatible). pybind11-only. |

### Class options

| Macro | Purpose |
|---|---|
| `APIARY_HOLDER(std::shared_ptr)` | Override the pybind11 holder type. Default is `std::unique_ptr`. |
| `APIARY_BUFFER_PROTOCOL` | Flip on pybind11's buffer protocol. Pair with `BUFFER_FROM`. |
| `APIARY_BUFFER_FROM(helper)` | Free function `helper(T&)` returning `py::buffer_info`; codegen wraps it in a `.def_buffer()` lambda. |
| `APIARY_IMPLICIT_FROM(Source)` | Emit `py::implicitly_convertible<Source, Class>()` after the binding. |
| `APIARY_DYNAMIC_ATTR` | Allow Python instances to carry arbitrary attributes. |
| `APIARY_NOCOPY` | Skip generation of the copy-ctor binding. |
| `APIARY_NOMOVE` | Skip generation of the move-ctor binding. |
| `APIARY_NO_BASES` | Force-skip emission of base-class arguments. Usually unnecessary — the emitter auto-skips bases that aren't themselves bound. |
| `APIARY_READONLY` | On a field — bind as `def_readonly` instead of `def_readwrite`. |

### Method / free-function options

| Macro | Purpose |
|---|---|
| `APIARY_RVP(reference_internal)` | Set `return_value_policy`. Argument is the unqualified policy name. |
| `APIARY_KEEP_ALIVE(0, 1)` | Emit `py::keep_alive<nurse, patient>()`. |
| `APIARY_RELEASE_GIL` | Wrap the call in `py::call_guard<py::gil_scoped_release>()`. |
| `APIARY_OPERATOR("__add__")` | Bind the method as a Python operator instead of a named function. |

### Properties

`APIARY_GETTER("name")` and `APIARY_SETTER("name")` get
merged into one `.def_property("name", &get, &set)` when the codegen
sees a matching name on a getter/setter pair. A `@getter` with no
matching `@setter` becomes a `.def_property_readonly`.

### Documentation

Doxygen comments (`///` or `/** */`) above an exposed declaration become
the Python docstring automatically. Override explicitly with
`APIARY_DOC("text")`.

### Template instantiation

Templated classes need an explicit instantiation directive — pybind11
binds concrete types, not templates.

**Cross-product** (`APIARY_INSTANTIATE`): each parameter list
is keyed by the *exact* C++ template-parameter name. The codegen matches
by name, not position, so the order in the macro is free. Python names
are auto-derived from the values.

```cpp
template <typename T, int rank>
class APIARY_EXPOSE
    APIARY_INSTANTIATE(Matrix,
        T(float, double),
        rank(1, 2))
Matrix { ... };
// Produces: Matrix_float_1, Matrix_float_2, Matrix_double_1, Matrix_double_2
```

**Single instantiation** (`APIARY_INSTANTIATE_AS`): pin one
concrete type to a chosen Python name. Use this when one template
parameter depends on another (e.g. `Alloc = std::allocator<T>`), which
a flat cross-product can't express.

```cpp
APIARY_INSTANTIATE_AS("Matrix2d_double",
                              Matrix<double, 2, std::allocator<double>>)
```

**Cross-product with name template** (`APIARY_INSTANTIATE_TEMPLATE`):
same matching rules; placeholders in the name template use the C++
template-parameter names too.

```cpp
template <typename Element, int Rank>
class APIARY_EXPOSE
    APIARY_INSTANTIATE_TEMPLATE("Block_{Element}_{Rank}",
        Block,
        Element(float, double),
        Rank(1, 2))
Block { ... };
// Produces: Block_float_1, Block_float_2, Block_double_1, Block_double_2
```

Placeholder values are sanitized to valid Python identifiers
(`std::complex<double>` → `std_complex_double`).

### Free-function template instantiation

`APIARY_INSTANTIATE_AS` also works on templated free functions —
each directive defines one instantiation. Multiple directives sharing a
Python name turn into a pybind11 overload set; the codegen picks the
right one at call site via Python's argument types.

```cpp
template <typename T>
APIARY_EXPOSE
APIARY_INSTANTIATE_AS("scale", mylib::Array<float>)
APIARY_INSTANTIATE_AS("scale", mylib::Array<double>)
void scale(typename T::ValueType factor, T *A);
```

#### Same-signature overloads → dtype dispatcher

When two or more `INSTANTIATE_AS` lines share a Python name AND their
argument signatures are identical (only the return type or value-type
differs), the codegen automatically collapses them into a single Python
entry that takes a `dtype="..."` kwarg and dispatches at runtime:

```cpp
template <typename T>
APIARY_EXPOSE
APIARY_INSTANTIATE_AS("zeros", float)
APIARY_INSTANTIATE_AS("zeros", double)
APIARY_INSTANTIATE_AS("zeros", std::complex<float>)
APIARY_INSTANTIATE_AS("zeros", std::complex<double>)
Array<T> zeros(std::string name, std::vector<size_t> dims);
// Python: zeros("X", [4, 4], dtype="float64")
```

Recognized dtype aliases (numpy convention): `float32`/`f4`/`f`/`single`
(float), `float64`/`f8`/`d` (double), `complex64`/`c8`/`F`
(complex<float>), `complex128`/`complex`/`c16`/`D` (complex<double>).
The default dtype is `float64` if `double` is in the group, otherwise
the first instantiation's first alias.

#### Bool template parameters → kwargs

For functions templated on leading `bool` parameters (e.g. `template
<bool TransA, bool TransB, typename T>`), pair
`APIARY_TEMPLATE_KWARGS` with `APIARY_INSTANTIATE_BOOLS`.
The codegen expands `2^N` combinations internally and emits one Python
entry per dtype taking each bool as a kw-only argument:

```cpp
template <bool TransA, bool TransB, typename T>
APIARY_EXPOSE
APIARY_TEMPLATE_KWARGS("trans_a", "trans_b")
APIARY_INSTANTIATE_BOOLS("gemm", mylib::Array<float>, float)
APIARY_INSTANTIATE_BOOLS("gemm", mylib::Array<double>, double)
void gemm(U alpha, T const &A, T const &B, U beta, T *C);
// Python: gemm(1.0, A, B, 0.0, C, trans_a=True, trans_b=False)
```

The first `INSTANTIATE_BOOLS` argument is the Python name (shared
across the bool fan-out); the rest are the *non-bool* template args.
The 2^N bool combinations are generated automatically; the codegen
then emits a single Python `def` per dtype with an internal `if`-chain
dispatcher.

### Member-template instantiation

Use `APIARY_INSTANTIATE_MEMBER_AS` to bind a templated method
with its own template parameters (independent of the enclosing class's
parameters). Multiple directives stack; same-signature ones with
recognized dtypes auto-merge into a `dtype=` dispatcher exactly like
free-function `INSTANTIATE_AS`.

```cpp
template <typename T>
class APIARY_EXPOSE Workspace { ... };

class Workspace {
    template <typename U>
    APIARY_EXPOSE
    APIARY_INSTANTIATE_MEMBER_AS("declare_array",
                                          U=mylib::Array<float>)
    APIARY_INSTANTIATE_MEMBER_AS("declare_array",
                                          U=mylib::Array<double>)
    U &declare_array(std::string name, std::vector<size_t> dims);
};
```

`APIARY_INSTANTIATE_MEMBER` (no `_AS`) is the same idea for
members whose own parameters depend on the class's. Argument is a
``Name=Type`` pair like ``Dim=std::vector<size_t>``.

### Variadic constructors

Templated classes often have parameter-pack constructors whose arity
depends on a template parameter. The codegen needs to know how many
arguments to bind per instantiation.

```cpp
template <typename T, size_t rank>
struct APIARY_EXPOSE
    APIARY_INSTANTIATE_AS("Matrix_double_2",
                                  Matrix<double, 2, std::allocator<double>>)
Matrix {
    template <typename... Dims>
    APIARY_EXPOSE
    APIARY_VARIADIC_FROM(rank, size_t)   // pack -> rank-many size_t args
    Matrix(std::string name, Dims... dims);
};
```

For `Matrix_double_2`, this binds a ctor with signature
`(std::string, size_t, size_t)`. For `Matrix_double_3` it would be
`(std::string, size_t, size_t, size_t)`.

The first arg names the template parameter that gives the count; the
second is the concrete C++ type each expanded slot should take. The
last function parameter is assumed to be the pack.

## Validation

The codegen emits Clang-style file:line errors and exits non-zero on
problems. Common cases:

| Error | Cause |
|---|---|
| `unknown parameter keyword 'X' (template parameters are: ...)` | Either a typo, or an upstream `#define` mangled the keyword before stringification. |
| `class name '<X>' in directive payload does not match` | Same cause: macro expansion changed the class name token. |
| `expected N parameter list(s), got M` | Number of `Param(...)` groups doesn't match the template signature. |
| `parameter keyword '<X>' specified more than once` | Duplicate group. |
| `missing parameter list for template parameter '<X>'` | A template parameter has no matching group. |

The strict name-match for `INSTANTIATE` / `INSTANTIATE_TEMPLATE` is the
load-bearing guard against random macro expansion. If some upstream
header has `#define Element WHATEVER`, the codegen sees `WHATEVER(...)`,
fails the match against the real `Element` parameter, and emits a
diagnostic instead of producing wrong bindings.

### Emitter diagnostics

Those errors all come from reading the *annotations*. A second class of
problem is only visible while writing the output: an input apiary parsed
happily but cannot represent in the target language. Those are reported
against the header that declares them, with a stable check id:

```
Handle.hpp:44:49: warning: apiary: method 'value' is &-qualified; the emitted
static_cast omits the qualifier and so names no overload. The generated code
will not compile. [unrepresentable-overload]
```

`--list-diagnostics` prints the check ids, their current severities and what
each means. Severity is per check and settable, so a project can ratchet:

```bash
apiary --list-diagnostics
apiary --diagnostic=skipped-entity=note ...      # opt in to an off-by-default check
apiary --diagnostic=unrepresentable-pack=error ...  # pin one, once it is clean
apiary --werror ...                              # promote every warning
```

They default to `warning` rather than `error` on purpose. Each describes output
that is already wrong, but apiary has always accepted these inputs and emitted
*something* for them; failing the build on day one would break consumers on
behavior they have been living with. Fix, then pin.

`--diagnostic` is validated before any work starts, so a typo in a check id or
severity exits 2 without writing a file. A run that emitted under a policy the
caller did not ask for would be the same silence the diagnostics exist to end.

Nothing is written until the exit status is known. Emission is where the
emitters discover what they cannot represent, so the status is not knowable
until every artifact has been generated — but it *is* knowable before any of
them reaches disk. A failing run therefore leaves no output: a build system that
reruns failed edges recovers either way, but a manual invocation or a tolerant
driver would otherwise be left with a partial file that looks fresh. Same
instinct as the empty-module refusal, which declines to write rather than leave
an empty-but-valid TU for the next step to consume happily.

### Parse errors and Clang's builtin headers

A header clang cannot parse fails the run in every mode, and nothing is written.
Clang recovers from errors, so the output would still look complete, but declarations after a fatal error are missing and a type it could not resolve reads as `int`.
`--allow-parse-errors` writes that output anyway, for a caller that would rather have part of an API than none of it.

The most common cause is Clang's builtin headers (`stddef.h`, `stdarg.h`, the intrinsics).
apiary's libclang needs the ones for its own LLVM major: the intrinsics headers call compiler builtins by name, and another major's can name one it does not have.
apiary looks next to its binary, in `lib/clang/<major>`, then in the LLVM it was built against; `apiary --print-resource-dir` says which it uses.
The conda-forge package depends on `clang-<major>`, which installs them next to the binary.
When they are missing, the error says so and names the package.

`apiary_detect_toolchain()` passes apiary those same headers.
Only when apiary has none does it borrow the build compiler's (or a conda `clang++`'s), warning when that Clang's major differs from apiary's.

## How it builds

1. **Configure** — `apiary_add_bindings()` emits an `add_custom_command` per
   set of headers, and `apiary_aggregate_extension()` writes the register
   header (`<prefix>all()`) and creates the `pybind11_add_module` (or
   `nanobind_add_module`) target. (In
   Einsums, `einsums_finalize_pybind()` calls these once per opted-in module.)

2. **Build** — ninja resolves the dependency chain:
   - the `apiary` tool builds first;
   - your C++ library/targets build;
   - for each unit, `apiary` runs over its headers and emits
     `<name>_pybind.cpp` (`<name>_nanobind.cpp` under nanobind), containing a
     `void <register-function>(py::module_ &m)`;
   - the consumer's `MAIN` and every generated TU compile;
   - the extension links them into one Python module.

Touching an annotated header re-fires only that unit's codegen edge (via the
`add_custom_command`'s `DEPENDS ${HEADERS}`) and re-links the extension.

## Known limitations

- **Default constructors with conditional `requires` clauses** that are
  compile-time `delete`d for some instantiations may emit spurious
  bindings. Use `APIARY_HIDE` to suppress per-method.
- **Cross-product with dependent parameters** can't be expressed
  (`Alloc = std::allocator<T>`). Fall back to one
  `APIARY_INSTANTIATE_AS` per concrete type.
- **System header detection** finds the C++ standard library by asking the
  build compiler for its include search list, which MSVC's `cl.exe` does not
  print; use `clang-cl` on Windows.
- **`requires requires { … }` clauses block doxygen attachment** —
  clang's `getRawCommentForDecl` doesn't associate `///` comments with
  a function template that has a nested requires-expression. Flatten
  to a single `requires (A && B && …)` clause and the comment (and
  thus the Python docstring) will attach.
- **Stub-side metafunction expansion** isn't supported — return types
  involving `RemoveComplexT<T>` and friends fall back to `Any` in the
  generated `.pyi`. The runtime binding is correct; only the static
  type information is reduced.

## Backend target

Apiary emits bindings for pybind11 (the default) or nanobind.
The same annotated headers bind to the same Python behavior under either, and the test suite holds both to that: every fixture has a golden and a compile check per backend, and every example is built and run against both.

On the command line, pass `--target`:

```bash
apiary --target nanobind --module myext header.hpp -- ...
```

With the CMake helpers the library is a build-wide choice, `APIARY_BACKEND` (`pybind11` by default, or `nanobind`), and a consumer writes nothing specific to either:

```cmake
find_package(Apiary 1.4 REQUIRED COMPONENTS tool)
apiary_find_backend()        # Python, and the library APIARY_BACKEND names
apiary_add_bindings(BINDING ...)
apiary_aggregate_extension(NAME _core MAIN src/main.cpp ...)
```

```cpp
#include <mylib/Modules.hpp>
APIARY_EXTENSION_MODULE(_core, m) { apiary_register_all(m); }
```

Configure with `-DAPIARY_BACKEND=nanobind` to switch.
A project that builds another Apiary consumer inside its own (Einsums builds Waggle) switches both at once: `apiary_find_backend()` makes its choice the default for the directories below.
A binding TU generated for one library and handed to an extension of the other is refused at configure time, rather than failing to link.

The pieces, all in `cmake/ApiaryBackend.cmake`:

- **`apiary_find_backend([BACKEND <lib>] [VERSION <v> [EXACT]])`** finds Python and the library, enforces its floor, and sets `APIARY_BACKEND` and `APIARY_BACKEND_VERSION` for the calling directory.
  `VERSION` is a minimum, or with `EXACT` the one release to accept, which is how an installed package pins its consumers to the release it was built with.
- **`apiary_add_extension_module(<name> [BACKEND <lib>] [NOMINSIZE] [OPTIONS <opt>...] <source>...)`** builds a hand-written module with the same library, as `apiary_aggregate_extension` builds the generated one.
- **`NOMINSIZE`**, on either of those, keeps the build type's optimization level; nanobind otherwise builds for size, and pybind11 never does.
  `ADD_MODULE_OPTIONS` / `OPTIONS` pass anything else to the library's own add-module command.
- **`APIARY_STABLE_ABI`** builds nanobind modules against the stable ABI (see below).

Both helpers define `APIARY_BACKEND_PYBIND11` or `APIARY_BACKEND_NANOBIND` on the module they create, which is what `<apiary/Python.hpp>` reads.
That header is for C++ written by hand - a module body, a library header that calls back into Python, a separate module - and names what the two libraries spell differently: `apiary::python::py` (the library's namespace, for the API both spell alike), `apiary::python::PythonError`, `apiary::python::import_module()`, `apiary::python::backend_name` and `APIARY_EXTENSION_MODULE`.
It and `ApiaryBackend.cmake` install with the annotations, so a package building modules against an installed library needs neither the tool nor Clang/LLVM.
The `greeter` example's `_extras` module is written this way.

Most of the output differs only in spelling (`py::` / `nb::`, `def_readwrite` / `def_rw`, `def_property` / `def_prop_rw`, `PYBIND11_MODULE` / `NB_MODULE`, `return_value_policy` / `rv_policy`).
Where nanobind differs in shape, Apiary writes the nanobind form:

- **Ref-qualified methods.**
  nanobind accepts no ref-qualified member pointer, so `&` and `const &` methods bind through a lambda, as `&&` methods already do under both.
- **Buffer protocol.**
  `APIARY_BUFFER_PROTOCOL_STD` becomes a `bf_getbuffer` type slot (`nb::type_slots`), so `memoryview`, `numpy.asarray` and `numpy.array` see the object zero copy, exactly as with pybind11's `.def_buffer`.
- **Index and iterator protocols.**
  `APIARY_INDEX_PROTOCOL_STD` and `APIARY_ITERATOR_STD` emit nanobind's slice, tuple and `make_iterator` APIs.
- **Exceptions.**
  `APIARY_EXCEPTION` becomes `nb::exception<T>`.
- **Holders.**
  `APIARY_HOLDER(std::shared_ptr)` and `APIARY_HOLDER(std::unique_ptr)` need no holder under nanobind, whose smart-pointer casters work for any bound class.
- **Standard-library casters.**
  nanobind has one caster header per type; Apiary includes the same set pybind11's `stl.h`, `functional.h`, `complex.h` and `stl/filesystem.h` cover, plus the smart pointers.

The Python API is meant to be the same under both libraries, so Apiary also smooths over the places where nanobind's defaults behave differently at call time:

- **Tuples of references.**
  nanobind turns `reference` or `reference_internal` on a `std::tuple` / `std::pair` return into `move`, which would move each element out of the object it refers to.
  Such a function binds through a lambda that casts each element with the policy, and with `self` as its parent for `reference_internal`, as pybind11 does.
- **None.**
  nanobind refuses `None` for an annotated argument unless told otherwise.
  Pointer, `std::optional`, `std::shared_ptr`, `std::function` and `bool` parameters are marked `.none()`, so `None` reaches them as it does under pybind11.
- **bool.**
  nanobind's `bool` accepts only `True` and `False`; pybind11's also accepts `numpy.bool_`, and when converting, anything with `__bool__`.
  `bool` and `std::function<bool(...)>` parameters bind through a caster that follows pybind11's rules, so a predicate returning `np.linalg.norm(r) > tol` works under both.
- **Errors from a buffer export.**
  An exception thrown while exporting a buffer reaches Python as a `BufferError` whose `__cause__` is the translated exception, as with pybind11.

Three things pybind11 can express and nanobind cannot are refused under `--target nanobind` with the `unsupported-on-target` diagnostic (an error by default):

- a class with more than one bound base (nanobind has single inheritance only);
- a holder other than `std::shared_ptr` / `std::unique_ptr`;
- `APIARY_BUFFER_FROM`, whose helper returns `pybind11::buffer_info` (use `APIARY_BUFFER_PROTOCOL_STD`, which works under both).

nanobind 3.0 or newer is required, and `apiary_find_backend` and the module helpers refuse an older one.
The 2.x headers call `alloca` from `nb_call.h` without declaring it under `Py_LIMITED_API` (2.8 fails to compile; 2.9 through 2.15 carry the same call), so 2.x cannot give a stable-ABI build, and a 2.x extension cannot share types with a 3.x one in any case.
The generated code is tested against 3.0.1 and 3.1.

`-DAPIARY_STABLE_ABI=ON` builds every nanobind module as one stable-ABI (abi3) extension that loads on every Python from 3.12 on; pybind11 has no stable-ABI build, so it is refused there.
`apiary_find_backend()` asks for the `Development.SABIModule` it links against.
Without that target nanobind quietly builds an ordinary per-version extension, so the helpers refuse the combination on Python 3.12 and newer.
The generated code compiles under `Py_LIMITED_API`; the examples are built and run that way.

## Stub generation (`.pyi`)

Every codegen invocation also produces a Python type-stub fragment,
emitted alongside the generated `.cpp`:

```
build/gen/mylib_core_pybind.cpp   # bindings
build/gen/mylib_core.pyi          # stub fragment
```

A finalize step (`scripts/apiary_aggregate_stubs.py`, located at `APIARY_SCRIPTS_DIR`)
runs as the `STUBS_TARGET` custom target `apiary_aggregate_extension` wires up,
after the extension is linked.
It splits each fragment by the `# %%submodule: <name>` sentinels the
emitter inserts and merges them into per-submodule files in the
package directory:

```
build/lib/mylib/
├── _core.cpython-…so      # the C extension
├── _core.pyi              # top-level entities
├── linalg.pyi             # entities tagged @module("linalg")
├── graph.pyi              # entities tagged @module("graph")
├── __init__.py / .pyi     # runtime + stub re-exporting _core
└── py.typed               # PEP 561 marker
```

The step merges exactly the `STUBS` passed to `apiary_aggregate_extension`, never every fragment in `FRAG_DIR`.
Removing a module from that list reruns it in an existing build tree: the removed module's classes leave `_core.pyi`, and a submodule's `.pyi` is deleted once no module contributes to it.

### What pyright sees

Type translation runs per-instantiation:

```python
# scale (free function with INSTANTIATE_AS for four dtypes)
@overload
def scale(factor: float, A: ArrayF) -> None: ...
@overload
def scale(factor: float, A: ArrayD) -> None: ...
@overload
def scale(factor: complex, A: ArrayC) -> None: ...
@overload
def scale(factor: complex, A: ArrayZ) -> None: ...

# zeros (auto-detected dtype dispatcher)
def zeros(name: str, dims: list[int], dtype: str = "float64") \
    -> ArrayF | ArrayD | ArrayC | ArrayZ: ...

# gemm (TEMPLATE_KWARGS bool fan-out)
@overload
def gemm(alpha: float, A: ArrayF, B: ArrayF, beta: float,
         C: ArrayF, *, trans_a: bool = False, trans_b: bool = False) -> None: ...
@overload
def gemm(alpha: complex, A: ArrayC, B: ArrayC, beta: complex,
         C: ArrayC, *, trans_a: bool = False, trans_b: bool = False) -> None: ...
```

Doxygen comments above an exposed declaration become Python docstrings.
`@getter` / `@setter` pairs become `@property` declarations.
Rich-comparison dunders (`__eq__`, `__lt__`, …) are widened to take
`object` to satisfy LSP — a stub typed `__eq__(self, other: Vec3)`
would otherwise trip pyright's `reportIncompatibleMethodOverride`.

### Cross-module name resolution

When a function in one module takes a type from another module (e.g. a
function taking an `Array<T>` whose binding lives in a different module),
the visitor records the external annotated class with `is_external=true`
purely for name resolution. The C++ emitter ignores externals (their
binding lives in the owning module's TU); the `.pyi` emitter uses
them to map `Array<float, std::allocator<float>>` →
`ArrayF` so cross-module signatures resolve without needing a
shared registry across codegen invocations.

### Type-resolution pipeline

For each per-instantiation parameter / return type, the `.pyi` emitter:

1. Substitutes template names on the **raw C++ type** (preserving forms
   like `typename T::ValueType` for re-resolution).
2. Tries the canonical (typedef-expanded) form via clang's
   `getCanonicalType()` if the as-written form fails — catches
   alias templates like `Array<T>` ↔
   `BasicArray<T, std::allocator<T>>`.
3. Inlines `typename Class<args>::ValueType` references with
   the class's first type argument.
4. Substitutes any known cpp_to_py-mapped class instantiation in
   nested types (so `std::tuple<Array<float>, ...>` reduces
   to `tuple[ArrayF, ...]`).
5. Falls back to `Any` when none of the above produces a Python-valid
   identifier — pyright will surface the gap rather than the stub
   silently mistyping.

### Python shell modules

Each Python submodule needs a tiny `.py` shell next to `_core.so`. The
recommended pattern uses PEP 562 `__getattr__` so the C extension
isn't loaded until first attribute access:

```python
# mylib/graph.py
import importlib as _importlib

def __getattr__(name):
    if name.startswith("_"):
        raise AttributeError(name)
    core = _importlib.import_module("._core.graph", "mylib")
    attr = getattr(core, name)
    globals()[name] = attr  # cache for subsequent lookups
    return attr
```

The generated `<sub>.pyi` describes the static surface; the `.py` shell
is just a runtime trampoline.

## Configure-time conditional bindings

Annotated headers can use `#if`/`#else`/`#endif` against any
configure-time define, including everything that
your project's config-define generator writes into `<mylib/Config.hpp>`. The
codegen tool runs Clang's full preprocessor and only sees the active
branch:

```cpp
#include <mylib/Config.hpp>
#include <mylib/cuda/DeviceAllocator.hpp>

template <typename T, size_t rank, typename Alloc>
struct APIARY_EXPOSE
    APIARY_INSTANTIATE_AS("Matrix_double_2",
                                  Matrix<double, 2, std::allocator<double>>)
#if defined(MYLIB_HAVE_CUDA)
    APIARY_INSTANTIATE_AS("Matrix_double_2_cuda",
                                  Matrix<double, 2, cuda::DeviceAllocator<double>>)
#endif
Matrix { ... };
```

When `MYLIB_WITH_CUDA=ON`, the GPU instantiation is added; toggling it
off and reconfiguring drops it. The generated `Defines.hpp` files are
in every codegen edge's `DEPENDS`, so re-configure → re-fire codegen
automatically. Also forwarded: every `INTERFACE_COMPILE_DEFINITIONS`
reachable from the module's MODULE_DEPENDENCIES (gets `-D` flags on the
codegen invocation).
- **Visibility warnings** when linking the extension (weak symbols across
  the consumer's library and the generated TU) are cosmetic on macOS;
  symbols still resolve correctly.

## Tool architecture (for contributors)

```
src/
  main.cpp              CLI driver: ClangTool + per-TU IR accumulation +
                        emit pass. Drives the post-IR passes
                        (compute_python_overloads, compute_properties)
                        before invoking the C++ and .pyi emitters.
                        Tracks total error count, exits non-zero.
  Visitor.hpp/.cpp      RecursiveASTVisitor that walks declarations,
                        filters by apiary: annotation, builds
                        the Module IR. Captures annotated classes from
                        outside the current module's headers as
                        ``is_external`` for cross-module name resolution.
  IR.hpp/.cpp           BoundClass / BoundMethod / BoundField / BoundEnum /
                        BoundFunction / BoundParam / BoundInstantiation /
                        BoundProperty / PythonOverload. Plus a
                        deterministic textual dump (``--dump-ir``).
  AnnotationParser.hpp/.cpp
                        Splits raw "apiary:<directive>:<args>"
                        payloads into structured Directive records.
                        Knows about free-form-tail directives (doc,
                        instantiate, holder) where the tail may contain
                        ':'.
  InstantiateParser.hpp/.cpp
                        Parses INSTANTIATE / INSTANTIATE_TEMPLATE
                        payloads into ParamGroup lists, respects nested
                        ``<>`` and ``()``. Provides cross_product and
                        sanitize_python_name helpers.
  TypeTranslator.hpp/.cpp
                        Wraps Clang's PrintingPolicy for fully-qualified
                        pretty C++ types. Also provides
                        ``translate_python_type`` (and the string-only
                        variant ``translate_python_type_string``) that
                        maps fundamentals + std containers to their
                        Python equivalents.
  DocExtractor.hpp/.cpp
                        Pulls doxygen text from
                        ASTContext::getRawCommentForDeclNoCache, strips
                        leading ``///``/``*`` markers and decoration
                        banner lines (``//////``, ``=====``, ``-----``).
  PythonOverloads.hpp/.cpp
                        Post-IR pass that decides how each free
                        function's raw instantiation list collapses
                        into Python entries: NonTemplate,
                        SingleInstantiation, OverloadSet,
                        DtypeDispatcher, TemplateKwargsDispatcher.
                        Both emitters consume the precomputed view.
  Properties.hpp/.cpp   Post-IR pass that walks each class's methods
                        and collapses @getter/@setter pairs into
                        BoundClass.properties.
  Emitter.hpp/.cpp      IR -> pybind11 C++ (or nanobind, via
                        ``--target``). Two output modes:
                        PYBIND11_MODULE(name, m) (standalone fixtures /
                        goldens) or void register_<Module>(py::module_ &m)
                        (autogen aggregator path). In-process
                        clang::format::reformat() picks up the project's
                        .clang-format.
  PyiEmitter.hpp/.cpp   IR -> Python .pyi stubs. Walks the same IR plus
                        the post-pass views, emits per-submodule blocks
                        delimited by ``# %%submodule: <name>`` sentinels
                        for the aggregator to split.

scripts/
  apiary_aggregate_stubs.py    Merges the ``*.pyi`` fragments it is
                        given (or ``@<file>`` listing them) by submodule
                        sentinel into per-submodule files in ``--pkg-dir``.
                        Writes a shared header per output and the
                        PEP-561 ``py.typed`` marker; with ``--manifest``,
                        deletes the stubs a previous run wrote that this
                        one no longer does.

tests/
  fixtures/             Annotated headers used by the emitter tests.
  golden/               Expected emitter output (regen with REGEN=1).
  run_smoke.sh          IR-dump substring assertions covering
                        BoundClass/BoundMethod/BoundFunction shapes,
                        property merge, submodule routing, Python
                        type translation, and default-value sanitization.
  run_golden.sh         pybind11 emitter golden-file diff.
```

## Adding a new directive

1. Define the macro in `include/apiary/Annotations.hpp`.
2. If its tail can contain `:`, add it to `directive_takes_free_form_tail`
   in `AnnotationParser.cpp`.
3. Handle it in the emitter (most directives are read via `DirectiveView`
   in `Emitter.cpp`'s class-body / method-emission helpers).
4. Add a fixture under `tests/fixtures/` and regenerate goldens
   (`REGEN=1 tests/run_golden.sh ...`).
5. Exercise it end-to-end with a Python smoke test (build an extension that
   uses the directive, then assert on the result).

## Examples

### Property pair → Python `@property`

```cpp
class APIARY_EXPOSE Resource {
public:
    /// Read-only-from-Python access to the underlying name.
    APIARY_GETTER("name")
    std::string const &get_name() const;

    /// Pythonic name setter.
    APIARY_SETTER("name")
    void set_name(std::string const &n);
};
```

Generated stub:

```python
class Resource:
    @property
    def name(self) -> str:
        """Read-only-from-Python access to the underlying name."""
        ...
    @name.setter
    def name(self, value: str) -> None: ...
```

### Operator overload

```cpp
class APIARY_EXPOSE Vec3 {
public:
    /// Component-wise equality.
    APIARY_EXPOSE APIARY_OPERATOR("__eq__")
    bool operator==(Vec3 const &other) const;
};
```

Generated stub (note the `object` widening for LSP compliance):

```python
class Vec3:
    def __eq__(self, other: object) -> bool:
        """Component-wise equality."""
        ...
```

### Submodule routing

```cpp
namespace APIARY_MODULE("graph") cg {

APIARY_EXPOSE
class Graph { ... };

APIARY_EXPOSE
void execute(Graph &g);

} // namespace cg
```

Both `Graph` and `execute` end up in `mylib.graph`. The aggregator
writes them into `build/lib/mylib/graph.pyi`. Anything outside the
namespace block (or any entity tagged with its own
`APIARY_MODULE("…")`) routes to the chosen submodule.

### Conditional binding gated on a config define

```cpp
#include <mylib/Config.hpp>

APIARY_EXPOSE
APIARY_INSTANTIATE_AS("Matrix_double_2",
                              Matrix<double, 2, std::allocator<double>>)
#if defined(MYLIB_HAVE_CUDA)
APIARY_INSTANTIATE_AS("Matrix_double_2_cuda",
                              Matrix<double, 2, cuda::DeviceAllocator<double>>)
#endif
template <typename T, size_t rank, typename Alloc>
class Matrix { ... };
```

Toggle `MYLIB_WITH_CUDA` and reconfigure — the codegen picks up the
`Defines.hpp` mtime change and re-fires automatically; the CUDA
instantiation appears (or disappears) in the generated bindings + stubs.
