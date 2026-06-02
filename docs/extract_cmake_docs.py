#!/usr/bin/env python3
#  Copyright (c) The Einsums Developers. All rights reserved.
#  Licensed under the MIT License. See LICENSE.txt in the project root.
"""Render the public ``apiary_*`` CMake helpers into reST.

apiary parses C++, not CMake — so the CMake consumer API is documented by
lifting the ``#`` doc-comment block above each ``function(apiary_...)`` in
cmake/ApiaryHelpers.cmake into docs/cmake.rst.
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
HELPERS = ROOT / "cmake" / "ApiaryHelpers.cmake"

HEADER = """\
.. _cmake_api:

=============
CMake helpers
=============

Consume apiary from CMake via ``find_package(Apiary)`` (installed) or
``add_subdirectory`` (vendored). Both provide:

- ``apiary::apiary`` — the codegen executable.
- ``apiary::annotations`` — an INTERFACE target carrying the ``APIARY_*``
  macro header; link it instead of hardcoding the include path.

…plus the helper functions below (lifted from their in-source documentation).
"""


def main() -> int:
    out_path = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "docs" / "cmake.rst"
    lines = HELPERS.read_text().splitlines()

    sections = []
    for i, line in enumerate(lines):
        m = re.match(r"function\((apiary_[A-Za-z0-9_]+)", line)
        if not m:
            continue
        name = m.group(1)
        # Walk back over the contiguous ``#`` comment block above the function.
        j = i - 1
        block = []
        while j >= 0 and lines[j].lstrip().startswith("#"):
            block.append(re.sub(r"^\s*#\s?", "", lines[j]))
            j -= 1
        block.reverse()
        sections.append((name, "\n".join(block).strip()))

    parts = [HEADER]
    for name, doc in sections:
        parts.append(f"\n``{name}``\n" + "-" * (len(name) + 4) + "\n")
        # Render the lifted comment as a literal block (it carries the usage
        # signature + description verbatim).
        body = "\n".join("   " + ln for ln in doc.splitlines())
        parts.append("::\n\n" + body + "\n")

    out_path.write_text("\n".join(parts) + "\n")
    print(f"wrote {len(sections)} CMake helper sections into {out_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
