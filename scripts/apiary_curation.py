#!/usr/bin/env python3
# ----------------------------------------------------------------------------------------------
# Copyright (c) The Einsums Developers. All rights reserved.
# Licensed under the MIT License. See LICENSE.txt in the project root for license information.
# ----------------------------------------------------------------------------------------------
"""Parse a content directory of authored Markdown into curation + articles.

Phase 3 of the DocC-like docs graph: the authored-content layer that turns a
symbol *reference* into *guided* documentation. Two kinds of file live under a
``--content-dir``:

- **Module curation** — a file whose stem is a documented dotted module
  (``einsums.linalg.md``). Its prose becomes the module page's overview, and a
  ``## Topics`` section organizes that module's symbols into named groups
  (DocC's model). Anything left uncurated still renders, auto-grouped by kind,
  so nothing silently disappears.
- **Articles** — any other ``.md`` file: a free-standing page (overview, guide,
  concept) that isn't tied to a single symbol.

Both may use ``[[Type/member]]`` symbol links; resolution happens later in the
renderer via the shared resolver. This module only parses structure; it emits
reST-ready prose (ATX headings converted) and the topic/link lists.

The accepted Markdown subset is small and line-based: ATX headings (``#``..),
paragraphs, and ``-`` bullet lists. A ``## Topics`` section is special; every
``### Group`` under it is a curation group whose ``[[ ]]`` links name members.
"""

from __future__ import annotations

import re
from dataclasses import dataclass, field
from pathlib import Path

from apiary_docs_resolve import find_links

_HEADING_RE = re.compile(r"^(#{1,4})\s+(.*?)\s*#*\s*$")
# reST underline character per heading level (within a page that already has a
# top-level title): level 1 is demoted to a section so it never clashes.
_RST_UNDERLINE = {1: "=", 2: "-", 3: "~", 4: '"'}


@dataclass
class TopicGroup:
    title: str
    tokens: list[str] = field(default_factory=list)  # ``[[ ]]`` inner tokens, in order


@dataclass
class ModuleCuration:
    module: str
    overview: str = ""               # reST-ready prose (``[[ ]]`` left for the renderer)
    topics: list[TopicGroup] = field(default_factory=list)

    def curated_tokens(self) -> list[str]:
        return [t for g in self.topics for t in g.tokens]


@dataclass
class TypeCuration:
    """Curation for a single class: overview prepended to its doc, plus member
    topic groups rendered under ``.. rubric::`` headings inside the class."""
    type_path: str
    overview: str = ""
    topics: list[TopicGroup] = field(default_factory=list)

    def curated_tokens(self) -> list[str]:
        return [t for g in self.topics for t in g.tokens]


@dataclass
class Article:
    slug: str       # page name (filename stem)
    title: str
    body: str       # reST-ready prose


def _heading(line: str) -> tuple[int, str] | None:
    m = _HEADING_RE.match(line)
    return (len(m.group(1)), m.group(2)) if m else None


def _to_rest(lines: list[str]) -> str:
    """Convert a Markdown block to reST: ATX headings become underlined titles;
    everything else (paragraphs, ``-`` lists, ``[[ ]]`` links) passes through."""
    out: list[str] = []
    for line in lines:
        h = _heading(line)
        if h:
            level, text = h
            out.append(text)
            out.append(_RST_UNDERLINE.get(level, '"') * max(len(text), 3))
        else:
            out.append(line)
    return "\n".join(out).strip("\n")


def _is_topics_header(line: str) -> bool:
    h = _heading(line)
    return bool(h and h[0] == 2 and h[1].strip().lower() == "topics")


def _parse_curation_body(text: str) -> tuple[str, list[TopicGroup]]:
    """Shared parse for module and type curation: overview prose + topic groups."""
    lines = text.splitlines()

    # Split at the ``## Topics`` header: prose before, topic groups after.
    topics_at = next((i for i, ln in enumerate(lines) if _is_topics_header(ln)), len(lines))
    prose_lines = lines[:topics_at]

    # Drop a leading level-1 title — the renderer supplies the heading.
    while prose_lines and not prose_lines[0].strip():
        prose_lines.pop(0)
    if prose_lines and (_heading(prose_lines[0]) or (0, ""))[0] == 1:
        prose_lines.pop(0)

    topics: list[TopicGroup] = []
    group: TopicGroup | None = None
    for line in lines[topics_at + 1:]:
        h = _heading(line)
        if h and h[0] <= 2:
            break  # next top-level section ends the Topics block
        if h and h[0] == 3:
            group = TopicGroup(title=h[1].strip())
            topics.append(group)
            continue
        if group is not None:
            group.tokens.extend(find_links(line))
    return _to_rest(prose_lines), topics


def parse_module_curation(module: str, text: str) -> ModuleCuration:
    overview, topics = _parse_curation_body(text)
    return ModuleCuration(module=module, overview=overview, topics=topics)


def parse_type_curation(type_path: str, text: str) -> TypeCuration:
    overview, topics = _parse_curation_body(text)
    return TypeCuration(type_path=type_path, overview=overview, topics=topics)


def parse_article(slug: str, text: str) -> Article:
    lines = text.splitlines()
    title = slug
    body_lines = lines
    for i, ln in enumerate(lines):
        h = _heading(ln)
        if h and h[0] == 1:
            title = h[1].strip()
            body_lines = lines[i + 1:]
            break
    return Article(slug=slug, title=title, body=_to_rest(body_lines))


def load_content(content_dir: str, known_modules: set[str],
                 known_types: set[str] = frozenset()) -> tuple[dict[str, ModuleCuration], dict[str, TypeCuration], list[Article]]:
    """Load a content directory: (module curations, type curations, articles).

    A ``.md`` whose stem is a documented module is module curation; whose stem is
    a documented class path is type curation; every other ``.md`` is an article."""
    module_curations: dict[str, ModuleCuration] = {}
    type_curations: dict[str, TypeCuration] = {}
    articles: list[Article] = []
    root = Path(content_dir)
    for path in sorted(root.rglob("*.md")):
        text = path.read_text(encoding="utf-8")
        stem = path.stem
        if stem in known_modules:
            module_curations[stem] = parse_module_curation(stem, text)
        elif stem in known_types:
            type_curations[stem] = parse_type_curation(stem, text)
        else:
            articles.append(parse_article(stem, text))
    return module_curations, type_curations, articles
