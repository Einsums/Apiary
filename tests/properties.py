#!/usr/bin/env python3
# ----------------------------------------------------------------------------------------------
# Copyright (c) The Einsums Developers. All rights reserved.
# Licensed under the MIT License. See LICENSE.txt in the project root for license information.
# ----------------------------------------------------------------------------------------------
"""Properties that must hold for every fixture, not just the ones with goldens.

A golden pins one input to one output. It says nothing about the *relationships*
between outputs, and those are where a whole class of defect lives: the binding
and the stub describing different APIs, or the tool producing different bytes
for the same input twice. Neither shows up as a golden diff, because each output
individually looks exactly like it always did.

Three properties, each with a concrete failure it is there to catch:

  agreement    The .cpp and the .pyi must expose the same (module, py_name)
               set. When they disagree, the stub lies about the API and
               pyright enforces that lie onto users - who then cannot call a
               function that exists, or call one that does not.

  idempotence  Running twice must produce identical bytes. This became
               load-bearing when the emitter started writing files only when
               their content changes: if emission were unstable, every build
               would rewrite every generated file and relink the world.

  order        Permuting the headers on the command line must not change the
               emitted module. Header order is a build-system detail - CMake
               globs, a developer reorders a list - and output that depends on
               it is output nobody can reproduce.

Invoked by run_properties.sh, which supplies the tool path and the fixtures.
"""
from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path

# The emitted TU is clang-formatted, which wraps a `.def(` and its name string
# onto separate lines whenever the call is long - and the long ones are exactly
# the interesting ones. So normalize whitespace to single spaces first and match
# against that; anchoring on line starts silently missed every wrapped call and
# reported the stub as having invented them.
#
# A MODULE-level def has an explicit receiver (`m.def(` or `_sub_x.def(`). A
# class member chains off the `py::class_<...>(...)` above it, so the character
# before `.def` is `)` and no receiver is captured - which is how the two are
# told apart here.
# The space after `(` matters: a def whose body is a lambda wraps as
# `m.def( "name", [](...` once flattened, and requiring `def("` missed exactly
# the dtype- and bool-dispatcher entries - the most heavily generated ones.
_DEF = re.compile(r'(\w+)\.def(?:_static)?\( ?"([^"]+)"')
# Template arguments nest (`py::class_<Tensor<double, std::allocator<double>>>`),
# so match lazily up to the `>(` that opens the registration call.
_CLASS = re.compile(r'py::class_<.*?>\( ?(\w+), "([^"]+)"')
_ENUM = re.compile(r'py::enum_<.*?>\( ?(\w+), "([^"]+)"')
_SUBMODULE = re.compile(r'auto (\w+) = (\w+)\.def_submodule\("([^"]+)"\)')


def _flat(cpp: str) -> str:
    """Collapse whitespace so wrapped calls read as one line."""
    return re.sub(r"\s+", " ", cpp)

# `# %%submodule: name` section markers, then `def name(` / `class Name:` at
# column 0 (module scope). Methods are indented and belong to their class, which
# the .cpp side reports as class members too - so both sides are compared at
# module scope only.
_PYI_SECTION = re.compile(r"^# %%submodule:\s*(\S*)\s*$", re.M)
_PYI_DEF = re.compile(r"^def (\w+)\(", re.M)
_PYI_CLASS = re.compile(r"^class (\w+)[:(]", re.M)


def run(tool: str, include: str, headers: list[str], module: str, extra: list[str] | None = None) -> tuple[str, str]:
    """Emit the binding TU and the stub for one header set. Returns (cpp, pyi)."""
    import tempfile

    with tempfile.TemporaryDirectory() as tmp:
        pyi_path = Path(tmp) / "out.pyi"
        cmd = [tool, "--module", module, "--stub-output", str(pyi_path), *(extra or []), *headers,
               "--", "-std=c++20", "-nostdinc++", f"-I{include}"]
        proc = subprocess.run(cmd, capture_output=True, text=True, check=False)
        if proc.returncode != 0:
            raise RuntimeError(f"apiary exited {proc.returncode} for {headers}:\n{proc.stderr}")
        return proc.stdout, pyi_path.read_text(encoding="utf-8")


def cpp_surface(cpp: str) -> set[str]:
    """The (module, py_name) pairs the binding TU registers, at module scope."""
    # Map the submodule variable names the emitter declares back to their dotted
    # paths, so `_sub_linalg` compares against the stub's `linalg` section.
    cpp = _flat(cpp)
    var_to_path: dict[str, str] = {}
    for var, parent, name in _SUBMODULE.findall(cpp):
        prefix = var_to_path.get(parent, "")
        var_to_path[var] = f"{prefix}.{name}" if prefix else name

    out: set[str] = set()
    for pattern in (_CLASS, _ENUM):
        for var, name in pattern.findall(cpp):
            out.add(f"{var_to_path.get(var, '')}:{name}")
    for var, name in _DEF.findall(cpp):
        # Only module-level receivers count; a class member's `.def` captures no
        # receiver, and `py::init` / helper calls are not `.def("name"` at all.
        if var not in var_to_path and var != "m":
            continue
        out.add(f"{var_to_path.get(var, '')}:{name}")
    return out


def pyi_surface(pyi: str) -> set[str]:
    """The (module, py_name) pairs the stub declares, at module scope."""
    out: set[str] = set()
    sections = list(_PYI_SECTION.finditer(pyi))
    for i, marker in enumerate(sections):
        module = marker.group(1) or ""
        end = sections[i + 1].start() if i + 1 < len(sections) else len(pyi)
        body = pyi[marker.end():end]
        for name in _PYI_DEF.findall(body):
            out.add(f"{module}:{name}")
        for name in _PYI_CLASS.findall(body):
            out.add(f"{module}:{name}")
    return out


# A parser over generated C++ is only as good as its patterns, and stale
# patterns fail SILENTLY here: they match nothing, the .cpp surface comes back
# empty, and the script reports that the stub invented every entry. That
# happened twice while this was being written - once because clang-format wraps
# a long `.def(` onto two lines, once because a def whose body is a lambda puts
# a space after the paren. So pin the parser on a sample of exactly what the
# emitter writes, including both wrapped forms.
_PARSER_SAMPLE = r"""
PYBIND11_MODULE(m, m) {
  auto _sub_core = m.def_submodule("core");
  auto _sub_deep = _sub_core.def_submodule("deep");

  py::enum_<ns::Layout>(m, "Layout").value("RowMajor", ns::Layout::RowMajor);

  py::class_<ns::Tensor<double, std::allocator<double>>>(
      _sub_core, "Tensor_double")
      .def(py::init<>())
      .def("norm", static_cast<double (ns::Tensor::*)() const>(&ns::Tensor::norm));

  m.def("plain", static_cast<int (*)(int)>(&ns::plain), py::arg("x"));
  _sub_deep.def(
      "wrapped_name",
      static_cast<int (*)(int)>(&ns::wrapped), py::arg("x"));
  m.def(
      "lambda_bodied",
      [](int seed, std::string const &dtype) -> py::object { return py::none(); },
      py::arg("seed"), py::arg("dtype") = "float64");
}
"""

_PARSER_EXPECTED = {
    ":Layout",                 # enum at the top level
    "core:Tensor_double",      # class in a submodule, nested template args
    ":plain",                  # ordinary module-level def
    "core.deep:wrapped_name",  # def wrapped by clang-format, nested submodule
    ":lambda_bodied",          # def whose body is a lambda (space after the paren)
    # and NOT "norm": a class member chains off py::class_ and has no receiver
}


def self_test() -> int:
    """Check the .cpp parser against a sample of the emitter's output shape."""
    got = cpp_surface(_PARSER_SAMPLE)
    if got != _PARSER_EXPECTED:
        print("FAIL: the .cpp parser does not match the emitter's output shape", file=sys.stderr)
        print(f"  missed:   {', '.join(sorted(_PARSER_EXPECTED - got)) or '-'}", file=sys.stderr)
        print(f"  spurious: {', '.join(sorted(got - _PARSER_EXPECTED)) or '-'}", file=sys.stderr)
        return 1
    return 0


def check(tool: str, include: str, fixtures: list[Path]) -> int:
    failures = 0

    def fail(msg: str) -> None:
        nonlocal failures
        print(f"FAIL: {msg}", file=sys.stderr)
        failures += 1

    for fx in fixtures:
        module = f"fixture_{fx.stem}"
        try:
            cpp, pyi = run(tool, include, [str(fx)], module)
        except RuntimeError as e:
            fail(str(e))
            continue

        # --- agreement ---
        in_cpp, in_pyi = cpp_surface(cpp), pyi_surface(pyi)
        if not in_cpp and not in_pyi:
            fail(f"{fx.name}: neither output registered anything - the patterns above have gone stale")
            continue
        if in_cpp != in_pyi:
            only_cpp = sorted(in_cpp - in_pyi)
            only_pyi = sorted(in_pyi - in_cpp)
            detail = []
            if only_cpp:
                detail.append(f"  bound but not in the stub: {', '.join(only_cpp)}")
            if only_pyi:
                detail.append(f"  in the stub but not bound: {', '.join(only_pyi)}")
            fail(f"{fx.name}: the binding and the stub describe different APIs\n" + "\n".join(detail))

        # --- idempotence ---
        cpp2, pyi2 = run(tool, include, [str(fx)], module)
        if cpp != cpp2 or pyi != pyi2:
            which = "the binding TU" if cpp != cpp2 else "the stub"
            fail(f"{fx.name}: emission is not idempotent - {which} differs between two identical runs")

    # --- order-independence ---
    # Needs more than one header in a module, which no single fixture gives, so
    # pair them up. Two headers in the same namespace with distinct entities is
    # exactly the shape a real multi-header module has.
    pairs = [(fixtures[i], fixtures[i + 1]) for i in range(0, len(fixtures) - 1, 2)]
    for a, b in pairs:
        module = "fixture_order"
        try:
            forward, forward_pyi = run(tool, include, [str(a), str(b)], module)
            reverse, reverse_pyi = run(tool, include, [str(b), str(a)], module)
        except RuntimeError as e:
            fail(str(e))
            continue
        if cpp_surface(forward) != cpp_surface(reverse):
            only_f = sorted(cpp_surface(forward) - cpp_surface(reverse))
            only_r = sorted(cpp_surface(reverse) - cpp_surface(forward))
            fail(f"{a.name} + {b.name}: the bound API depends on header order\n"
                 f"  only when {a.name} is first: {', '.join(only_f) or '-'}\n"
                 f"  only when {b.name} is first: {', '.join(only_r) or '-'}")
        if pyi_surface(forward_pyi) != pyi_surface(reverse_pyi):
            fail(f"{a.name} + {b.name}: the stub's API depends on header order")

    return failures


def main(argv: list[str]) -> int:
    if len(argv) < 4:
        print("usage: properties.py <apiary-binary> <include-dir> <fixture.hpp>...", file=sys.stderr)
        return 64
    tool, include = argv[1], argv[2]
    fixtures = [Path(p) for p in argv[3:]]
    if self_test() != 0:
        return 1
    failures = check(tool, include, fixtures)
    if failures:
        print(f"{failures} property failure(s)", file=sys.stderr)
        return 1
    print(f"properties: OK ({len(fixtures)} fixture(s))")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
