#!/usr/bin/env python3
#  Copyright (c) The Einsums Developers. All rights reserved.
#  Licensed under the MIT License. See LICENSE.txt in the project root.
"""Dogfood: run apiary on its own headers to produce the C++ API reST.

For every apiary header, invoke ``apiary --emit-cpp-docs-json`` and render the
result with scripts/apiary_render_cpp_rst.py into docs/api/<name>.rst. The toolchain
include paths libtooling needs are probed here (mirrors
apiary_detect_toolchain) so this runs standalone on macOS and Linux/CI.

    python docs/gen_self_docs.py --tool <apiary-binary> [--out-dir docs/api]
"""
from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SCRIPTS = ROOT / "scripts"

# (page title, header path relative to ROOT, --source-include name)
HEADERS = [
    ("Annotations (APIARY_* macros)", "include/apiary/Annotations.hpp", "apiary/Annotations.hpp"),
    ("IR — bound declarations",        "src/IR.hpp",                "IR.hpp"),
    ("Visitor",                        "src/Visitor.hpp",           "Visitor.hpp"),
    ("AnnotationParser",               "src/AnnotationParser.hpp",  "AnnotationParser.hpp"),
    ("InstantiateParser",              "src/InstantiateParser.hpp", "InstantiateParser.hpp"),
    ("DocComment",                     "src/DocComment.hpp",        "DocComment.hpp"),
    ("DocExtractor",                   "src/DocExtractor.hpp",      "DocExtractor.hpp"),
    ("MacroScanner",                   "src/MacroScanner.hpp",      "MacroScanner.hpp"),
    ("TypeTranslator",                 "src/TypeTranslator.hpp",    "TypeTranslator.hpp"),
    ("Properties",                     "src/Properties.hpp",        "Properties.hpp"),
    ("PythonOverloads",                "src/PythonOverloads.hpp",   "PythonOverloads.hpp"),
    ("DocsJson",                       "src/DocsJson.hpp",          "DocsJson.hpp"),
    ("Emitter",                        "src/Emitter.hpp",           "Emitter.hpp"),
    ("PyiEmitter",                     "src/PyiEmitter.hpp",        "PyiEmitter.hpp"),
]


def _run(cmd: list[str]) -> str:
    return subprocess.run(cmd, capture_output=True, text=True).stdout.strip()


def toolchain_flags() -> list[str]:
    """The -resource-dir / -isystem flags libtooling needs to parse real C++.

    Mirrors cmake/ApiaryHelpers.cmake:apiary_detect_toolchain — resource-dir
    (clang builtins), the C++ stdlib search dirs from the compiler, and the
    LLVM/Clang dev headers (apiary's own src includes them)."""
    flags = ["-std=c++17"]
    resource = _run(["clang++", "-print-resource-dir"])
    if resource:
        flags += ["-resource-dir", resource]
    llvm_inc = _run(["llvm-config", "--includedir"])
    if llvm_inc:
        flags += ["-I", llvm_inc]
    flags += ["-I", str(ROOT / "src"), "-I", str(ROOT / "include")]

    # C++ stdlib search dirs from the real compiler. macOS: forward the whole
    # list (and rely on it instead of -isysroot, to avoid a dual-libc++ clash);
    # Linux: keep only the C++ library dirs.
    import platform
    is_mac = platform.system() == "Darwin"
    probe = subprocess.run(
        ["clang++", "-std=c++17", "-E", "-x", "c++", "-v", "/dev/null"],
        capture_output=True, text=True,
    )
    in_search = False
    for line in probe.stderr.splitlines():
        if "#include <...> search starts here:" in line:
            in_search = True
        elif "End of search list." in line:
            in_search = False
        elif in_search:
            d = line.strip()
            if d.endswith("(framework directory)") or not d or not Path(d).exists():
                continue
            if is_mac or "/c++" in d:
                flags += ["-isystem", d]
    return flags


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--tool", required=True, help="apiary binary")
    ap.add_argument("--out-dir", default=str(ROOT / "docs" / "api"))
    args = ap.parse_args()

    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    flags = toolchain_flags()
    renderer = SCRIPTS / "apiary_render_cpp_rst.py"

    pages: list[tuple[str, str]] = []  # (title, rst-stem)
    for title, rel, src_inc in HEADERS:
        header = ROOT / rel
        stem = src_inc.replace("/", "_").replace(".", "_")
        json_path = out_dir / f"{stem}.json"
        rst_path = out_dir / f"{stem}.rst"
        cmd = [args.tool, "--emit-cpp-docs-json", "--module", "apiary",
               "--source-include", src_inc, str(header), "--", *flags]
        res = subprocess.run(cmd, capture_output=True, text=True)
        json_path.write_text(res.stdout)
        if not res.stdout.strip():
            print(f"  WARN {rel}: empty docs JSON\n{res.stderr.strip()[:300]}", file=sys.stderr)
            continue
        render = subprocess.run(
            [sys.executable, str(renderer), str(json_path),
             "--title", title, "--output", str(rst_path)],
            capture_output=True, text=True)
        if render.returncode != 0:
            print(f"  render failed for {rel}: {render.stderr.strip()[:300]}", file=sys.stderr)
            continue
        pages.append((title, stem))
        print(f"  ok  {rel} -> {rst_path.name}")

    # api/index.rst with a toctree of the generated pages.
    toc = "\n".join(f"   {stem}" for _, stem in pages)
    (out_dir / "index.rst").write_text(
        ".. _cpp_api:\n\n"
        "=========\n"
        "C++ API\n"
        "=========\n\n"
        "Generated from apiary's own headers by ``apiary --emit-cpp-docs-json``\n"
        "(apiary documenting itself).\n\n"
        ".. toctree::\n"
        "   :maxdepth: 1\n\n"
        f"{toc}\n"
    )
    print(f"wrote {len(pages)} C++ API pages into {out_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
