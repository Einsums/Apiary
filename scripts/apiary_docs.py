#!/usr/bin/env python3
# ----------------------------------------------------------------------------------------------
# Copyright (c) The Einsums Developers. All rights reserved.
# Licensed under the MIT License. See LICENSE.txt in the project root for license information.
# ----------------------------------------------------------------------------------------------
"""One-command driver for the apiary docs pipeline.

Collapses the four steps — extract the Python layer, merge fragments, (optional)
check links, render reST — into a single invocation for consumers not using the
CMake helpers. It orchestrates the same bundled scripts the CMake path runs, so
there is no behavior drift.

The C++ side is emitted separately (it needs the compiler flags libtooling
wants): run ``apiary --emit-docs-json --module <m> <headers> -- <flags>`` once
per module and pass the resulting fragment(s) via ``--cpp-docs``.

Example::

    apiary_docs.py --outdir build/docs \\
        --package mathx --package-dir python/mathx --source-root . \\
        --cpp-docs build/mathx_core.docs.json \\
        --content-dir content --check-links \\
        --py-source-url-template 'https://github.com/org/pkg/blob/main/{file}#L{line}'
"""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent


def _run(script: str, *args) -> None:
    subprocess.run([sys.executable, str(HERE / script), *map(str, args)], check=True)


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--outdir", required=True, help="directory for the rendered .rst (and the merged docs.json)")
    # Python frontend (optional): extract a package's hand-written layer.
    ap.add_argument("--package", help="top-level package import name to extract")
    ap.add_argument("--package-dir", help="filesystem directory of --package (contains __init__.py)")
    ap.add_argument("--source-root", help="record location.file relative to this (for clean source links)")
    # Pre-emitted fragments to fold into the merge.
    ap.add_argument("--cpp-docs", nargs="*", default=[], help="C++ docs-JSON fragments (apiary --emit-docs-json)")
    ap.add_argument("--py-docs", nargs="*", default=[], help="additional Python docs-JSON fragments")
    # Rendering.
    ap.add_argument("--content-dir", help="authored Markdown: curation + articles")
    ap.add_argument("--cpp-source-url-template", help="C++ source-link URL template ({file},{line})")
    ap.add_argument("--py-source-url-template", help="Python source-link URL template ({file},{line})")
    ap.add_argument("--check-links", action="store_true", help="report unresolved [[ ]] links over the merged graph")
    ap.add_argument("--strict", action="store_true", help="with --check-links, fail on any unresolved link")
    args = ap.parse_args(argv)

    outdir = Path(args.outdir)
    outdir.mkdir(parents=True, exist_ok=True)
    fragments: list = [*args.cpp_docs, *args.py_docs]

    # 1. Python frontend (if a package was given).
    if args.package:
        if not args.package_dir:
            ap.error("--package requires --package-dir")
        py_frag = outdir / f"{args.package}.py.docs.json"
        extra = ["--source-root", args.source_root] if args.source_root else []
        _run("apiary_py_extract.py", "--package", args.package, "--package-dir", args.package_dir, *extra, "-o", py_frag)
        fragments.append(py_frag)

    if not fragments:
        ap.error("nothing to document: pass --package and/or --cpp-docs / --py-docs")

    # 2. Merge into one canonical docs.json.
    merged = outdir / "docs.json"
    _run("apiary_merge_docs_json.py", "-o", merged, *fragments)

    # 3. Optional link check (non-fatal unless --strict).
    if args.check_links:
        cmd = [sys.executable, str(HERE / "apiary_doc_lint.py"), "--check-links", "--select", "unresolved-reference"]
        if args.strict:
            cmd.append("--strict")
        cmd.append(str(merged))
        rc = subprocess.run(cmd).returncode
        if args.strict and rc != 0:
            return rc

    # 4. Render.
    render = ["apiary_render_docs_rst.py", "--outdir", outdir]
    if args.content_dir:
        render += ["--content-dir", args.content_dir]
    if args.cpp_source_url_template:
        render += ["--cpp-source-url-template", args.cpp_source_url_template]
    if args.py_source_url_template:
        render += ["--py-source-url-template", args.py_source_url_template]
    render.append(merged)
    _run(*render)

    print(f"apiary_docs: wrote the Python API reference into {outdir}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
