#!/usr/bin/env python3
# ----------------------------------------------------------------------------------------------
# Copyright (c) The Einsums Developers. All rights reserved.
# Licensed under the MIT License. See LICENSE.txt in the project root for license information.
# ----------------------------------------------------------------------------------------------
"""reST well-formedness checking for the doc text apiary emits.

apiary's Doxygen-to-reST conversion (``src/DocComment.cpp``) produces the
``doc_structured`` strings in the docs-JSON IR, and the renderers splice those
strings straight into ``.rst``. Nothing between the two ever asked whether the
result is *valid* reST, so a doc comment that converts badly stayed silent
through the tool, through the JSON, through the renderer, and surfaced three
build steps later as a docutils warning pointing at a generated file with no
back-reference to the header that caused it.

This module closes that gap: it parses each fragment with docutils and reports
the structural complaints. Two deliberate scoping choices:

- **Parse only, never publish.** Transforms are what resolve references,
  substitutions and citations, and a fragment legitimately refers to targets
  defined elsewhere on the rendered page. Running the parser alone reports
  syntax and block structure - what apiary is actually responsible for - and
  says nothing about resolution, which it is not.
- **Stub the vocabulary apiary does not own.** docutils knows nothing of
  Sphinx's directives and roles. Every Sphinx directive ``DocComment.cpp`` can
  emit is registered here as a permissive stub so its *body* is parsed - that
  body is exactly where malformed bullet lists live. Anything still unknown
  (a role from the renderer, whatever an author spliced through ``@rst``) is
  filtered out by message class rather than reported as a defect.
"""
from __future__ import annotations

from dataclasses import dataclass

try:
    import docutils.frontend
    import docutils.nodes
    import docutils.utils
    from docutils.parsers.rst import Directive, Parser, directives

    HAVE_DOCUTILS = True
except ImportError:  # pragma: no cover - exercised only on installs without docutils
    HAVE_DOCUTILS = False

MISSING_DOCUTILS = (
    "the 'malformed-rest' check needs docutils (pip install docutils, or "
    "conda install docutils); pass --select to run the other checks without it"
)

# Sphinx-only directives that apiary itself emits. ``DocComment.cpp`` renders
# @see/@sa -> seealso and @versionadded{...} and friends -> version*. docutils
# would reject each as an unknown directive AND swallow its body as a literal
# block, so a malformed list inside a @note or a @par would go unnoticed. A
# permissive stub gets the body parsed instead.
_PARSED_DIRECTIVES = (
    "seealso",
    "versionadded",
    "versionchanged",
    "versionremoved",
    "deprecated",
)

# Directives whose content is NOT reST and must never be parsed as it. @code
# renders to code-block, and source code is full of characters reST reads as
# markup: a C++ `C *= 2` is an unterminated emphasis if you parse it. Sphinx
# treats these bodies as literal, so we must too, or the check invents defects
# in every documented example.
_LITERAL_DIRECTIVES = (
    "code-block",
    "autosummary",
    "toctree",
    "literalinclude",
)

# Message classes about vocabulary apiary does not own: a Sphinx role from the
# renderer, a directive spliced in through @rst, a substitution defined in the
# page template. Reporting these would make the check unusable without
# reimplementing Sphinx, and none of them is a defect in the conversion.
_IGNORED_PREFIXES = (
    "Unknown directive type",
    "No directive entry for",
    "Unknown interpreted text role",
    "No role entry for",
    "Undefined substitution referenced",
    "Unknown target name",
    "Duplicate explicit target name",
    "Duplicate implicit target name",
)

_LEVEL_NAMES = {0: "debug", 1: "info", 2: "warning", 3: "error", 4: "severe"}


@dataclass(frozen=True)
class RestProblem:
    """One docutils complaint about a fragment."""

    line: int  # 1-based line WITHIN the fragment (0 when docutils gives none)
    level: str  # docutils level name: warning / error / severe
    message: str


_registered = False


def _register_stubs() -> None:
    """Register the permissive stub for each Sphinx directive apiary emits.

    Defined here rather than at module scope so this file stays importable
    without docutils - the caller decides whether a missing docutils is fatal,
    and it cannot decide that if the import already blew up.
    """
    global _registered
    if _registered:
        return

    class _Base(Directive):
        """Accept any argument/option shape a Sphinx directive might carry."""

        has_content = True
        required_arguments = 0
        optional_arguments = 9
        final_argument_whitespace = True
        option_spec: dict = {}

    class _ParsedStub(_Base):
        """Parse the body as normal reST - defects inside it are ours to report."""

        def run(self):
            node = docutils.nodes.container()
            self.state.nested_parse(self.content, self.content_offset, node)
            return [node]

    class _LiteralStub(_Base):
        """Consume the body without parsing - it is code, not markup."""

        def run(self):
            return [docutils.nodes.literal_block("", "\n".join(self.content))]

    for name in _PARSED_DIRECTIVES:
        directives.register_directive(name, _ParsedStub)
    for name in _LITERAL_DIRECTIVES:
        directives.register_directive(name, _LiteralStub)
    _registered = True


def available() -> bool:
    """True when docutils is importable and the check can run."""
    return HAVE_DOCUTILS


def check_fragment(text: str, *, min_level: int = 2) -> list[RestProblem]:
    """Parse one reST fragment, returning the structural complaints.

    ``min_level`` is a docutils severity: 2 is WARNING, which is where the
    interesting structure errors live ("Bullet list ends without a blank
    line", "Inline literal start-string without end-string"). INFO is noise.
    """
    if not HAVE_DOCUTILS:
        raise RuntimeError(MISSING_DOCUTILS)
    if not (text or "").strip():
        return []
    _register_stubs()

    parser = Parser()
    settings = docutils.frontend.get_default_settings(Parser)
    settings.report_level = 1  # observe everything; filtering is ours to do
    settings.halt_level = 5  # never raise - collect and keep going
    settings.warning_stream = False  # nothing on stderr; the caller reports
    document = docutils.utils.new_document("<doc-fragment>", settings)

    collected: list = []
    document.reporter.attach_observer(collected.append)
    parser.parse(text, document)

    problems: list[RestProblem] = []
    for msg in collected:
        level = int(msg["level"])
        if level < min_level:
            continue
        body = msg.children[0].astext() if msg.children else ""
        # docutils reports the "No directive entry" INFO and the "Unknown
        # directive type" ERROR as separate messages; both start with a
        # recognizable prefix, so one filter covers the pair.
        if body.startswith(_IGNORED_PREFIXES):
            continue
        # Collapse the message to a single line: docutils appends the offending
        # source text to some messages, and a diagnostic must stay grep-able.
        flat = " ".join(body.split())
        problems.append(RestProblem(int(msg.get("line", 0) or 0), _LEVEL_NAMES.get(level, "error"), flat))
    return problems
