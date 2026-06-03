#!/usr/bin/env python3
#  Copyright (c) The Einsums Developers. All rights reserved.
#  Licensed under the MIT License. See LICENSE.txt in the project root.
"""Doc-quality validator over apiary's public-API JSON IR.

apiary already parses both the *signature* (real parameter names, template
parameters, return type) and the *doc comment* (``@param`` / ``@tparam`` /
``@return`` / ``@throws``) and emits both in the ``--emit-cpp-docs-json``
output. This tool cross-checks the two so documentation drift becomes a CI
gate rather than something discovered at render time:

  - a ``@param``/``@tparam`` that names something the signature doesn't have
    (a typo or a parameter that was renamed/removed) -> error;
  - a parameter/template-parameter left undocumented when its siblings are
    documented (partial docs -> likely an oversight) -> warning;
  - ``@return`` text on a ``void`` function -> warning;
  - a malformed ``@throws`` with no exception type -> warning.

Input is one or more docs-JSON files (the output of ``apiary
--emit-cpp-docs-json``). Diagnostics are printed as
``file:line:col: severity: message [check]`` (editor/CI parseable). Exit
status is non-zero when any error is found (or any warning under --strict).

    apiary --emit-cpp-docs-json --module m header.hpp -- <flags> > m.json
    doc_lint.py m.json
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import dataclass

# check id -> default severity
SEVERITY = {
    "unknown-param": "error",
    "unknown-tparam": "error",
    "missing-param": "warning",
    "missing-tparam": "warning",
    "returns-on-void": "warning",
    "malformed-throws": "warning",
}


@dataclass
class Finding:
    file: str
    line: int
    col: int
    severity: str
    check: str
    message: str

    def sort_key(self) -> tuple:
        return (self.file, self.line, self.col, self.check)

    def text(self) -> str:
        return f"{self.file}:{self.line}:{self.col}: {self.severity}: {self.message} [{self.check}]"


# ``@param[in,out] name`` -> ``name``; ``@param a, b`` -> ``a``, ``b``.
_DIR_PREFIX = re.compile(r"^\[[^\]]*\]\s*")


def doc_names(entries: list[dict]) -> list[str]:
    """Parameter/tparam names a doc comment claims, normalized.

    Strips a leading Doxygen direction prefix (``[in]``/``[out]``/``[in,out]``)
    and splits the rare comma-grouped ``@param a, b`` form into separate names.
    """
    out: list[str] = []
    for e in entries:
        raw = _DIR_PREFIX.sub("", (e.get("name") or "").strip())
        for piece in raw.split(","):
            nm = piece.strip()
            if nm:
                out.append(nm)
    return out


def real_param_names(callable_obj: dict) -> list[str]:
    """Named parameters in the signature (unnamed params are skipped)."""
    names = []
    for p in callable_obj.get("params", []):
        # functions/methods: params are objects {name,type,...}; function-like
        # macros: params are bare strings.
        nm = p.get("name") if isinstance(p, dict) else p
        if nm:
            names.append(nm)
    return names


_VOID = re.compile(r"^\s*(const\s+|volatile\s+)*void\s*$")


def returns_void(callable_obj: dict) -> bool:
    if callable_obj.get("is_constructor") or callable_obj.get("is_destructor"):
        return False
    rt = callable_obj.get("return_type_canonical") or callable_obj.get("return_type") or ""
    return bool(_VOID.match(rt))


def loc(entity: dict) -> tuple[str, int, int]:
    location = entity.get("location") or {}
    return location.get("file", "<unknown>"), int(location.get("line", 0)), int(location.get("column", 0))


def check_callable(entity: dict, *, kind: str, findings: list[Finding]) -> None:
    """Cross-check one function/method/constructor/function-like macro."""
    ds = entity.get("doc_structured") or {}
    name = entity.get("qualified_name") or entity.get("name") or "<anonymous>"
    f, ln, col = loc(entity)

    def add(check: str, msg: str) -> None:
        findings.append(Finding(f, ln, col, SEVERITY[check], check, msg))

    # --- parameters ---
    real = real_param_names(entity)
    documented = doc_names(ds.get("params", []))
    real_set, doc_set = set(real), set(documented)
    for d in documented:
        if d not in real_set:
            add("unknown-param", f"{name}: @param '{d}' does not name a parameter")
    # only flag undocumented params when SOME params are already documented
    # (a fully-undocumented parameter list is the ratchet's concern, not ours)
    if documented:
        for r in real:
            if r not in doc_set:
                add("missing-param", f"{name}: parameter '{r}' is undocumented")

    # --- template parameters ---
    real_tp = [t for t in entity.get("template_params", []) if t]
    doc_tp = doc_names(ds.get("tparams", []))
    real_tp_set, doc_tp_set = set(real_tp), set(doc_tp)
    for d in doc_tp:
        if d not in real_tp_set:
            add("unknown-tparam", f"{name}: @tparam '{d}' does not name a template parameter")
    if doc_tp:
        for r in real_tp:
            if r not in doc_tp_set:
                add("missing-tparam", f"{name}: template parameter '{r}' is undocumented")

    # --- return ---
    if (ds.get("returns") or "").strip() and returns_void(entity):
        add("returns-on-void", f"{name}: @return documented on a void {kind}")

    # --- throws ---
    for t in ds.get("throws", []):
        if not (t.get("name") or "").strip():
            add("malformed-throws", f"{name}: @throws with no exception type")


def walk_class(cls: dict, findings: list[Finding]) -> None:
    for m in cls.get("constructors", []):
        check_callable(m, kind="constructor", findings=findings)
    for m in cls.get("methods", []):
        check_callable(m, kind="method", findings=findings)
    for nested in cls.get("nested_classes", []):
        walk_class(nested, findings)


def lint_module(doc: dict, findings: list[Finding]) -> None:
    for fn in doc.get("functions", []):
        check_callable(fn, kind="function", findings=findings)
    for cls in doc.get("classes", []):
        walk_class(cls, findings)
    for mac in doc.get("macros", []):
        if mac.get("is_function_like"):
            check_callable(mac, kind="macro", findings=findings)


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description="Cross-check apiary docs JSON for doc/signature drift.")
    ap.add_argument("files", nargs="+", help="docs-JSON files (apiary --emit-cpp-docs-json output)")
    ap.add_argument("--format", choices=("text", "json"), default="text")
    ap.add_argument("--strict", action="store_true", help="treat warnings as errors for the exit status")
    ap.add_argument("--select", default="", help="comma-separated check ids to run (default: all)")
    args = ap.parse_args(argv)

    selected = set(filter(None, args.select.split(","))) or set(SEVERITY)
    unknown = selected - set(SEVERITY)
    if unknown:
        ap.error(f"unknown check id(s): {', '.join(sorted(unknown))}; valid: {', '.join(sorted(SEVERITY))}")

    findings: list[Finding] = []
    for path in args.files:
        try:
            with open(path) as fh:
                doc = json.load(fh)
        except (OSError, json.JSONDecodeError) as e:
            print(f"doc_lint: cannot read {path}: {e}", file=sys.stderr)
            return 2
        lint_module(doc, findings)

    findings = [f for f in findings if f.check in selected]
    findings.sort(key=Finding.sort_key)

    n_err = sum(f.severity == "error" for f in findings)
    n_warn = sum(f.severity == "warning" for f in findings)

    if args.format == "json":
        json.dump([f.__dict__ for f in findings], sys.stdout, indent=2)
        sys.stdout.write("\n")
    else:
        for f in findings:
            print(f.text())
        print(f"doc_lint: {n_err} error(s), {n_warn} warning(s)", file=sys.stderr)

    return 1 if (n_err or (args.strict and n_warn)) else 0


if __name__ == "__main__":
    raise SystemExit(main())
