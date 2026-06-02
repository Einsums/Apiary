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
find_package(Apiary REQUIRED)          # or: add_subdirectory(external/apiary)

target_link_libraries(mylib PUBLIC apiary::annotations)   # the APIARY_* header

# Probe the build compiler once for the system/stdlib include paths libtooling
# needs (sets APIARY_SYSTEM_FLAGS).
apiary_detect_toolchain(CXX_STANDARD 20)

# Generate the binding TU (+ .pyi + docs JSON). DEPENDS_TARGETS supplies the
# transitive -I/-D from your library's usage requirements.
apiary_add_bindings(
    BINDING DOCS_JSON
    HEADERS           ${CMAKE_SOURCE_DIR}/mylib/include/mylib/Greeter.hpp
    SOURCE_INCLUDES   mylib/Greeter.hpp
    REGISTER_FUNCTION apiary_register_mylib
    DEPENDS_TARGETS   mylib
    OUTPUT_NAME       mylib
    CXX_STANDARD      20
    OUT_BINDING _tu  OUT_STUB _stub  OUT_DOCS_JSON _docs
)

# Assemble one or more modules into a single Python extension. You supply MAIN
# (the PYBIND11_MODULE body); the helper generates the register header it
# includes and creates the pybind11 target.
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
PYBIND11_MODULE(_core, m) { apiary_register_all(m); }
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

## How it builds

1. **Configure** — `apiary_add_bindings()` emits an `add_custom_command` per
   set of headers, and `apiary_aggregate_extension()` writes the register
   header (`<prefix>all()`) and creates the `pybind11_add_module` target. (In
   Einsums, `einsums_finalize_pybind()` calls these once per opted-in module.)

2. **Build** — ninja resolves the dependency chain:
   - the `apiary` tool builds first;
   - your C++ library/targets build;
   - for each unit, `apiary` runs over its headers and emits
     `<name>_pybind.cpp`, containing a `void <register-function>(py::module_ &m)`;
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
- **System header detection** assumes Clang's `-print-resource-dir` is
  available and (on macOS) `xcrun --show-sdk-path`. A conda env with
  `clangdev` + `llvmdev` satisfies both. Other setups may need to set
  `APIARY_CLANG_RESOURCE_DIR` / `APIARY_SYSROOT`
  manually before the first configure.
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

apiary can emit code against either pybind11 (default) or
nanobind. Pass `--target {pybind11,nanobind}` on the command line:

```bash
apiary --target nanobind --module myext header.hpp -- ...
```

The output differs in:

- **Headers** — `<pybind11/...>` vs `<nanobind/...>`, with nanobind's
  STL bindings split per-type (`<nanobind/stl/string.h>`,
  `<nanobind/stl/vector.h>`, etc.)
- **Module macro** — `PYBIND11_MODULE` vs `NB_MODULE`
- **Namespace** — `py::` vs `nb::`
- **Return value policy** — `py::return_value_policy::reference_internal`
  vs `nb::rv_policy::reference_internal`
- **Buffer protocol** — pybind11 emits `.def_buffer()` lambdas; nanobind
  doesn't have an equivalent directive (use `nb::ndarray<>` for tensor
  protocol instead). `APIARY_BUFFER_FROM` directives are
  silently dropped under the nanobind target.

The `apiary_aggregate_extension` helper uses pybind11 today.
Switching the pipeline to nanobind requires also swapping
`pybind11_add_module` for `nanobind_add_module` and the matching
`find_package(nanobind)`. The `--target` flag is what makes the rest of
that switch a one-line change in the cmake hook.

## Stub generation (`.pyi`)

Every codegen invocation also produces a Python type-stub fragment,
emitted alongside the generated `.cpp`:

```
build/gen/mylib_core_pybind.cpp   # bindings
build/gen/mylib_core.pyi          # stub fragment
```

A finalize step (`scripts/aggregate_stubs.py`, located at `APIARY_SCRIPTS_DIR`)
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
  aggregate_stubs.py    Reads every ``*.pyi`` fragment in
                        ``--frag-dir`` and merges by submodule sentinel
                        into per-submodule files in ``--pkg-dir``.
                        Writes a shared header per output and the
                        PEP-561 ``py.typed`` marker.

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
