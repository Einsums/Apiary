#!/usr/bin/env python3
# ----------------------------------------------------------------------------------------------
# Copyright (c) The Einsums Developers. All rights reserved.
# Licensed under the MIT License. See LICENSE.txt in the project root for license information.
# ----------------------------------------------------------------------------------------------
"""Emit random-but-valid annotated headers from a small grammar.

The fixture corpus contains what someone thought to write down. That is a real
limit: every defect this repo has found so far was found by a shape a person
chose to add, which means the shapes nobody thought of are exactly the ones
still broken. A generator does not have taste, and that is its whole value.

What it emits is deliberately narrow: only constructs apiary CLAIMS to support,
combined in ways nobody would bother to enumerate by hand - a const-and-
ref-qualified method with a defaulted enum argument on a templated class in a
submodule, and so on. Anything it emits that apiary mishandles is a real defect,
because the input is inside the documented contract.

Determinism is not a nicety here. A fuzz failure you cannot reproduce is a
rumour, so everything derives from an explicit seed, the seed is printed with
any failure, and re-running with it reproduces the exact header.
"""
from __future__ import annotations

import argparse
import random
import sys

LICENSE = """//----------------------------------------------------------------------------------------------
// Copyright (c) The Einsums Developers. All rights reserved.
// Licensed under the MIT License. See LICENSE.txt in the project root for license information.
//----------------------------------------------------------------------------------------------
"""

# Types apiary maps to a Python type. Deliberately no std:: templates: the
# generated header is compiled against real pybind11 by the fuzz runner, and the
# hermetic std stand-ins the fixture corpus uses collide with the real libc++.
SCALARS = ["int", "double", "float", "bool", "char", "unsigned"]
# Value categories a parameter can be declared with. Each has to survive into a
# static_cast in the binding and an annotation in the stub.
PARAM_FORMS = ["{t}", "{t} const &", "{t} &", "{t} const"]

DEFAULTS = {
    "int": ["0", "-1", "42"],
    "double": ["0.0", "1.0 / 3.0", "-2.5"],
    "float": ["0.0f", "1.5f"],
    "bool": ["true", "false"],
    "char": ["'x'", "'\\0'"],
    "unsigned": ["0u", "7u"],
}


class Gen:
    """One header's worth of generation state."""

    def __init__(self, rng: random.Random, index: int) -> None:
        self.rng = rng
        self.index = index
        self.lines: list[str] = []
        self.n = 0

    def name(self, stem: str) -> str:
        self.n += 1
        return f"{stem}_{self.n}"

    def scalar(self) -> str:
        return self.rng.choice(SCALARS)

    def param(self, slot: int) -> str:
        t = self.scalar()
        form = self.rng.choice(PARAM_FORMS).format(t=t)
        # A default only goes on a by-value or const-ref parameter, and only on
        # a type we know how to spell one for.
        if self.rng.random() < 0.3 and "&" not in form.replace("const &", "") and t in DEFAULTS:
            if "&" not in form or form.endswith("const &"):
                return f"{form} p{slot} = {self.rng.choice(DEFAULTS[t])}"
        return f"{form} p{slot}"

    def params(self, low: int = 0, high: int = 3) -> str:
        count = self.rng.randint(low, high)
        # Once one parameter has a default, every later one must too, or the C++
        # does not compile - a rule the generator has to respect to stay inside
        # the contract it is testing.
        out: list[str] = []
        seen_default = False
        for i in range(count):
            p = self.param(i)
            if seen_default and "=" not in p:
                t = self.scalar()
                p = f"{t} p{i} = {self.rng.choice(DEFAULTS[t])}"
            seen_default = seen_default or "=" in p
            out.append(p)
        return ", ".join(out)

    # ---- entity emitters -------------------------------------------------

    def free_function(self) -> None:
        n = self.name("fn")
        ret = self.rng.choice(SCALARS + ["void"])
        extras = []
        if self.rng.random() < 0.25:
            extras.append("APIARY_RELEASE_GIL")
        if self.rng.random() < 0.2:
            extras.append('APIARY_MODULE("sub")')
        self.lines.append(f"/// Free function {n}.")
        self.lines.append(f"APIARY_EXPOSE {' '.join(extras)} {ret} {n}({self.params()});")
        self.lines.append("")

    def enum(self):
        n = self.name("Kind")
        scoped = "class " if self.rng.random() < 0.7 else ""
        # An UNSCOPED enum puts its enumerators in the enclosing namespace, so
        # two of them sharing `V0` is a redefinition. Key the names to the enum
        # so the generated header stays valid C++ - the harness checks that, and
        # a fuzzer that emits bad input only teaches you to ignore it.
        vals = [f"{n}_V{i}" for i in range(self.rng.randint(1, 4))]
        self.lines.append(f"/// Enum {n}.")
        self.lines.append(f"enum {scoped}APIARY_EXPOSE {n} {{")
        for v in vals:
            self.lines.append(f"    {v}, ///< Enumerator {v}.")
        self.lines.append("};")
        self.lines.append("")
        return n, scoped != "", vals

    def klass(self, enums: list) -> None:
        n = self.name("Cls")
        mod = f' APIARY_MODULE("sub{self.rng.randint(0, 2)}")' if self.rng.random() < 0.3 else ""
        self.lines.append(f"/// Class {n}.")
        self.lines.append(f"class APIARY_EXPOSE{mod} {n} {{")
        self.lines.append("  public:")
        self.lines.append(f"    APIARY_EXPOSE {n}();")
        if self.rng.random() < 0.4:
            self.lines.append(f"    APIARY_EXPOSE explicit {n}({self.scalar()} seed);")

        for _ in range(self.rng.randint(1, 4)):
            m = self.name("m")
            ret = self.rng.choice(SCALARS + ["void"])
            quals = self.rng.choice(["", " const", " const", " &", " const &", " &&", " const &&", " const noexcept"])
            self.lines.append(f"    /// Method {m}.")
            self.lines.append(f"    APIARY_EXPOSE {ret} {m}({self.params(0, 2)}){quals};")

        # A defaulted enum argument, which forces the enum's binding to be
        # emitted before this class's.
        if enums and self.rng.random() < 0.4:
            ename, scoped, vals = self.rng.choice(enums)
            spelled = f"{ename}::{vals[0]}" if scoped else vals[0]
            m = self.name("with_enum")
            self.lines.append(f"    /// Takes an enum with a default.")
            self.lines.append(f"    APIARY_EXPOSE void {m}({ename} k = {spelled});")

        if self.rng.random() < 0.3:
            f = self.name("field")
            ro = " APIARY_READONLY" if self.rng.random() < 0.5 else ""
            self.lines.append(f"    /// Field {f}.")
            self.lines.append(f"    APIARY_EXPOSE{ro} {self.scalar()} {f};")

        self.lines.append("};")
        self.lines.append("")

    def templated_function(self) -> None:
        n = self.name("tfn")
        types = self.rng.sample(["float", "double"], k=self.rng.randint(1, 2))
        insts = " ".join(f'APIARY_INSTANTIATE_AS("{n}", {t})' for t in types)
        self.lines.append(f"/// Templated function {n}.")
        self.lines.append("template <typename T>")
        self.lines.append(f"APIARY_EXPOSE {insts} T {n}(T value, {self.params(0, 1) or 'int unused = 0'});")
        self.lines.append("")

    def variadic_function(self) -> None:
        n = self.name("vfn")
        arity = self.rng.randint(1, 3)
        args = ", ".join(["double"] * arity)
        lead = "double lead, " if self.rng.random() < 0.5 else ""
        self.lines.append(f"/// Variadic {n}, pinned to {arity} argument(s).")
        self.lines.append("template <typename... Ts>")
        self.lines.append(f'APIARY_EXPOSE APIARY_INSTANTIATE_AS("{n}", {args}) double {n}({lead}Ts... vals);')
        self.lines.append("")

    def operators(self) -> None:
        n = self.name("Val")
        self.lines.append(f"/// Value type {n} with operators.")
        self.lines.append(f"class APIARY_EXPOSE {n} {{")
        self.lines.append("  public:")
        self.lines.append(f"    APIARY_EXPOSE {n}();")
        for dunder, sig in [
            ("__add__", f"{n} operator+({n} const &rhs) const"),
            ("__eq__", f"bool operator==({n} const &rhs) const"),
            ("__getitem__", "double operator[](int i) const"),
            ("__bool__", "explicit operator bool() const"),
        ]:
            if self.rng.random() < 0.6:
                self.lines.append(f"    /// Operator {dunder}.")
                self.lines.append(f'    APIARY_EXPOSE APIARY_OPERATOR("{dunder}") {sig};')
        self.lines.append("};")
        self.lines.append("")


def generate(seed: int, index: int) -> str:
    rng = random.Random(seed * 1000 + index)
    g = Gen(rng, index)

    enums = []
    for _ in range(rng.randint(0, 2)):
        enums.append(g.enum())

    emitters = [g.free_function, lambda: g.klass(enums), g.templated_function, g.variadic_function, g.operators]
    for _ in range(rng.randint(2, 6)):
        rng.choice(emitters)()

    body = "\n".join(g.lines).rstrip()
    return (
        f"{LICENSE}\n"
        f"// GENERATED by tests/generate_headers.py - seed {seed}, index {index}.\n"
        f"// Do not edit; regenerate with the seed above.\n\n"
        "#pragma once\n\n"
        "#include <apiary/Annotations.hpp>\n\n"
        "namespace einsums::fuzz {\n\n"
        f"{body}\n\n"
        "} // namespace einsums::fuzz\n"
    )


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--seed", type=int, required=True)
    ap.add_argument("--index", type=int, default=0)
    ap.add_argument("-o", "--output", default="-")
    args = ap.parse_args(argv[1:])

    text = generate(args.seed, args.index)
    if args.output == "-":
        sys.stdout.write(text)
    else:
        with open(args.output, "w", encoding="utf-8", newline="\n") as fh:
            fh.write(text)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
