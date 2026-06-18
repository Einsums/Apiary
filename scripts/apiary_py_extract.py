#!/usr/bin/env python3
# ----------------------------------------------------------------------------------------------
# Copyright (c) The Einsums Developers. All rights reserved.
# Licensed under the MIT License. See LICENSE.txt in the project root for license information.
# ----------------------------------------------------------------------------------------------
"""Static Python extraction frontend for apiary.

The second extraction frontend. Where the C++ tool walks annotated C++ with a
Clang frontend, this walks a consumer package's hand-written ``.py`` files with
the standard-library ``ast`` module ONLY — no ``import``, no ``inspect``, no
built extension. That preserves apiary's no-runtime property: documenting pure
Python never executes it.

It emits a docs-JSON *fragment* in the shared schema (``docs/docs_json_schema.md``,
``schema_version`` 3) with every entity tagged ``origin: "python"``. The merge
stage (``apiary_merge_docs_json.py``) folds these fragments in alongside the C++
frontend's fragments so Python-origin symbols land on the SAME submodule pages
as the bound C++ surface.

The documentable Python layer is the shell a consumer adds ON TOP of the
extension: convenience wrappers, helpers, non-trivial ``__init__`` content,
pure-Python modules. The bound surface (already documented from C++ ``///``
comments) is NOT re-parsed here — that would be reading apiary's own output
backwards.

Usage::

    apiary_py_extract.py --package einsums --package-dir path/to/einsums --output einsums.py.docs.json
    apiary_py_extract.py --package einsums --package-dir path/to/einsums file1.py file2.py
"""

from __future__ import annotations

import argparse
import ast
import io
import json
import re
import sys
import tokenize
from pathlib import Path

SCHEMA_VERSION = 5  # keep in lockstep with apiary_docs_schema.SCHEMA_VERSION

PREFIX = "py_extract"


def log(msg: str) -> None:
    print(f"{PREFIX}: {msg}", file=sys.stderr)


# ── Doc comments for data (#: / # ...) ───────────────────────────────────────
#
# Module variables, class attributes, and enum members are documented with
# comments, not docstrings — and comments live outside the AST. We index the
# file's comments by line with ``tokenize`` and attach, to each data
# assignment, the contiguous standalone comment block immediately above it
# (Sphinx's ``#:`` convention, and plain ``#`` blocks), or a trailing comment on
# the assignment line. Set per file in ``extract_file``.
_CTX: dict = {"comments": {}, "code_rows": set()}

# Trivial token types that don't count as "code on this row".
_TRIVIAL_TOKENS = frozenset({
    tokenize.COMMENT, tokenize.NL, tokenize.NEWLINE,
    tokenize.INDENT, tokenize.DEDENT, tokenize.ENCODING, tokenize.ENDMARKER,
})


def index_comments(source: str) -> tuple[dict[int, str], set[int]]:
    """Map line-number -> comment text, plus the set of lines that carry code."""
    comments: dict[int, str] = {}
    code_rows: set[int] = set()
    try:
        for tok in tokenize.generate_tokens(io.StringIO(source).readline):
            if tok.type == tokenize.COMMENT:
                comments[tok.start[0]] = tok.string
            elif tok.type not in _TRIVIAL_TOKENS:
                code_rows.add(tok.start[0])
    except (tokenize.TokenError, IndentationError, SyntaxError):
        pass  # best-effort; a parse-clean file rarely trips this
    return comments, code_rows


def _strip_comment_marker(c: str) -> str:
    return re.sub(r"^#:?\s?", "", c).rstrip()


def _is_section_header(line: str) -> bool:
    """A short Title-Case comment with no terminal punctuation reads as a section
    divider (``# Logging``, ``# ComputeGraph Passes``), not a description."""
    words = line.split()
    if not words or len(words) > 3 or line.rstrip()[-1:] in ".:?!,":
        return False
    return all(w[0].isupper() for w in words if w[:1].isalpha())


def data_doc(node: ast.AST) -> str:
    """The doc comment for a data assignment: the contiguous standalone comment
    block immediately above it (``#:`` or plain ``#``, with a leading section
    header stripped), else a trailing comment on its last line."""
    comments = _CTX["comments"]
    code_rows = _CTX["code_rows"]
    if not comments:
        return ""
    block: list[str] = []
    row = getattr(node, "lineno", 0) - 1
    while row in comments and row not in code_rows:
        block.append(_strip_comment_marker(comments[row]))
        row -= 1
    block.reverse()
    while block and _is_section_header(block[0]):
        block.pop(0)  # drop a leading "# Logging"-style divider
    text = " ".join(s for s in block if s).strip()
    if text:
        return text
    end = getattr(node, "end_lineno", None) or getattr(node, "lineno", 0)
    if end in comments and end in code_rows:
        return _strip_comment_marker(comments[end]).strip()
    return ""


# ── Module-path resolution (the shared join key) ─────────────────────────────


def dotted_module(file: Path, package: str, package_dir: Path) -> str:
    """The fully-qualified dotted module a ``.py`` file defines.

    ``einsums/linalg.py`` -> ``einsums.linalg``; ``einsums/__init__.py`` ->
    ``einsums``; ``einsums/sub/pkg/__init__.py`` -> ``einsums.sub.pkg``. This is
    the canonical module-join key the merge stage groups on, identical in form
    to the C++ frontend's APIARY_MODULE-routed ``submodule``.
    """
    rel = file.resolve().relative_to(package_dir.resolve())
    parts = list(rel.parts)
    if parts and parts[-1] == "__init__.py":
        parts = parts[:-1]
    elif parts:
        parts[-1] = parts[-1][:-3] if parts[-1].endswith(".py") else parts[-1]
    return ".".join([package, *parts])


def _recorded_path(file: Path, source_root: Path | None) -> Path:
    """The path recorded in ``location.file``. With ``--source-root`` it is made
    relative to that root so the renderer can build clean repo source links;
    otherwise the path is recorded as given."""
    if source_root is None:
        return file
    try:
        return file.resolve().relative_to(source_root.resolve())
    except ValueError:
        return file


# ── Docstring -> structured form (numpydoc) ──────────────────────────────────
#
# Origin selects grammar AT THE PRODUCER: the C++ frontend parses Doxygen
# (DocComment.cpp), this parses numpydoc-style sections — and BOTH emit the same
# `doc_structured` shape (brief/detail/params/tparams/returns/throws), so the
# merge stage and renderer stay grammar-agnostic. A numpydoc section is a header
# line followed by a line of dashes ("Parameters\n----------"); entries are
# "name : type" at the section's base indent with an indented description.

_PARAM_SECTIONS = {"parameters", "other parameters", "keyword arguments", "keyword args"}
_RETURN_SECTIONS = {"returns", "yields"}
_RAISE_SECTIONS = {"raises", "warns"}


def _find_sections(lines: list[str]) -> list[tuple[str, int]]:
    """Locate numpydoc 'Header\\n-----' blocks: returns [(header_lower, idx)]."""
    out: list[tuple[str, int]] = []
    for idx in range(len(lines) - 1):
        head = lines[idx].strip()
        under = lines[idx + 1].strip()
        if head and under and set(under) == {"-"} and len(under) >= 3:
            out.append((head.lower(), idx))
    return out


def _split_paragraphs(lines: list[str]) -> list[list[str]]:
    paras: list[list[str]] = []
    cur: list[str] = []
    for line in lines:
        if line.strip():
            cur.append(line)
        elif cur:
            paras.append(cur)
            cur = []
    if cur:
        paras.append(cur)
    return paras


def _collapse(lines: list[str]) -> str:
    return " ".join(" ".join(lines).split())


def _parse_entries(body: list[str]) -> list[dict]:
    """Parse a numpydoc parameter/raises section body into name+description.

    Entry headers ("name : type", possibly "a, b : int") sit at the body's base
    indent; description lines are indented further. The type half is dropped —
    Python annotations already come from the signature."""
    nonblank = [ln for ln in body if ln.strip()]
    if not nonblank:
        return []
    base = min(len(ln) - len(ln.lstrip()) for ln in nonblank)

    entries: list[dict] = []
    names: list[str] | None = None
    desc: list[str] = []

    def flush() -> None:
        nonlocal names, desc
        if names is not None:
            text = _collapse(desc)
            for nm in names:
                entries.append({"name": nm, "description": text})
        names, desc = None, []

    for ln in body:
        if not ln.strip():
            continue
        indent = len(ln) - len(ln.lstrip())
        if indent <= base:
            flush()
            namepart = ln.strip().split(":", 1)[0]
            names = [n.strip() for n in namepart.split(",") if n.strip()]
            desc = []
        else:
            desc.append(ln.strip())
    flush()
    return entries


def _returns_text(body: list[str]) -> str:
    entries = _parse_entries(body)
    # Prefer descriptions; fall back to the bare-prose form (a Returns section
    # with only an indented description and no "name : type" header line).
    pieces = [e["description"] or e["name"] for e in entries]
    return " ".join(p for p in pieces if p)


def doc_structured(raw: str | None) -> dict:
    text = raw or ""
    lines = text.split("\n")
    secs = _find_sections(lines)
    first = secs[0][1] if secs else len(lines)

    preamble = _split_paragraphs(lines[:first])
    brief = _collapse(preamble[0]) if preamble else ""
    detail_parts = ["\n".join(p) for p in preamble[1:]]

    params: list[dict] = []
    returns = ""
    throws: list[dict] = []
    for k, (name, idx) in enumerate(secs):
        end = secs[k + 1][1] if k + 1 < len(secs) else len(lines)
        body = lines[idx + 2:end]
        if name in _PARAM_SECTIONS:
            params += _parse_entries(body)
        elif name in _RETURN_SECTIONS:
            r = _returns_text(body)
            returns = f"{returns} {r}".strip() if returns else r
        elif name in _RAISE_SECTIONS:
            throws += _parse_entries(body)
        else:
            # Keep unrecognized sections (Notes, Examples, ...) so nothing is lost.
            detail_parts.append(f"{lines[idx].strip()}\n" + "\n".join(body).rstrip())

    detail = "\n\n".join(p for p in detail_parts if p.strip()).strip()
    return {"brief": brief, "detail": detail, "params": params, "tparams": [], "returns": returns, "throws": throws}


# ── Signature extraction ─────────────────────────────────────────────────────


def _param(name: str, py_type: str = "", default: str | None = None) -> dict:
    """A docs-JSON param. Python-origin fills the python forms (``py_type`` from
    the annotation source, ``default``/``default_py`` from the default expr) and
    leaves the C++ type fields empty (binding-centric fields a Python node lacks).

    ``py_type`` is the annotation recovered verbatim as source via
    ``ast.unparse`` — e.g. ``list[int]``, ``ArrayF``, ``np.ndarray``. The
    renderer feeds these through the SAME name-resolution path it uses for C++
    py_types (``KNOWN_TYPES``/``pythonize``), so an annotation naming a
    documented class cross-links to that class's page.
    """
    return {
        "name": name,
        "type": "",            # C++ form — empty for Python origin
        "type_canonical": "",  # C++ form — empty for Python origin
        "py_type": py_type,
        "default": default,
        "default_py": default,
    }


def _unparse(node: ast.AST | None) -> str | None:
    return None if node is None else ast.unparse(node)


def _annotation(arg: ast.arg) -> str:
    return _unparse(arg.annotation) or ""


def params_from_args(args: ast.arguments, *, drop_first: bool = False) -> list[dict]:
    """Walk ``ast.arguments`` correctly into ordered param dicts.

    Handles every slot: positional-only, positional-or-keyword, ``*args``,
    keyword-only, ``**kwargs``. ``defaults`` align to the TAIL of the combined
    positional list; ``kw_defaults`` is parallel to ``kwonlyargs`` with ``None``
    marking a required keyword-only arg. Annotations are recovered as source via
    ``ast.unparse``. ``drop_first`` removes the leading ``self``/``cls`` of a
    bound method.
    """
    out: list[dict] = []

    positional = list(args.posonlyargs) + list(args.args)
    # defaults align to the tail of the ORIGINAL positional list; compute the
    # first-defaulted index against the original counts even when self/cls is
    # dropped, so a dropped leading arg never shifts the alignment.
    first_default = len(positional) - len(args.defaults)
    start = 0
    if drop_first and positional and positional[0].arg in ("self", "cls"):
        start = 1
    for i in range(start, len(positional)):
        a = positional[i]
        d = args.defaults[i - first_default] if i >= first_default else None
        out.append(_param(a.arg, _annotation(a), _unparse(d)))

    if args.vararg is not None:
        out.append(_param("*" + args.vararg.arg, _annotation(args.vararg)))

    for a, d in zip(args.kwonlyargs, args.kw_defaults):
        out.append(_param(a.arg, _annotation(a), _unparse(d)))

    if args.kwarg is not None:
        out.append(_param("**" + args.kwarg.arg, _annotation(args.kwarg)))

    return out


# ── Entity builders ──────────────────────────────────────────────────────────


def _location(file: Path, node: ast.AST) -> dict:
    return {"file": str(file), "line": getattr(node, "lineno", 0), "column": getattr(node, "col_offset", 0) + 1}


# Sphinx availability directives in a docstring.
_VERSIONADDED_RE = re.compile(r"^\s*\.\.\s+versionadded::\s*(\S+)", re.M)
_DEPRECATED_RE = re.compile(r"^\s*\.\.\s+deprecated::\s*(\S+)?", re.M)
_AVAIL_DIRECTIVE_RE = re.compile(r"^(\s*)\.\.\s+(versionadded|deprecated)::.*$")


def strip_availability_directives(text: str | None) -> str:
    """Remove ``.. versionadded::`` / ``.. deprecated::`` directive blocks from
    docstring prose — they are lifted into the structured ``availability`` field
    and rendered from there, so leaving them in the detail double-renders."""
    lines = (text or "").split("\n")
    out: list[str] = []
    i = 0
    while i < len(lines):
        m = _AVAIL_DIRECTIVE_RE.match(lines[i])
        if not m:
            out.append(lines[i])
            i += 1
            continue
        base = len(m.group(1))
        i += 1
        # Drop the directive's own body (blank or more-indented lines).
        while i < len(lines) and (not lines[i].strip() or (len(lines[i]) - len(lines[i].lstrip())) > base):
            i += 1
    return "\n".join(out)


def _deprecated_decorator(node) -> tuple[bool, str | None]:
    """Detect a ``@deprecated`` / ``@warnings.deprecated`` / ``@typing.deprecated``
    decorator (PEP 702) and its message argument, if any."""
    for d in getattr(node, "decorator_list", []):
        target = d.func if isinstance(d, ast.Call) else d
        if ast.unparse(target).split(".")[-1] != "deprecated":
            continue
        if isinstance(d, ast.Call):
            for a in d.args:
                if isinstance(a, ast.Constant) and isinstance(a.value, str):
                    return True, a.value
        return True, None
    return False, None


def availability(node, raw: str | None) -> dict:
    """Structured availability for a Python entity, in the shared schema shape.

    Sources: a ``@deprecated`` decorator (with its message) and the Sphinx
    ``.. versionadded::`` / ``.. deprecated::`` directives in the docstring. The
    C++ frontend fills the same shape from ``@since`` / ``@deprecated``."""
    deprecated, note = _deprecated_decorator(node)
    text = raw or ""
    deprecated_since = None
    dep = _DEPRECATED_RE.search(text)
    if dep:
        deprecated = True
        deprecated_since = dep.group(1) or None
    added = _VERSIONADDED_RE.search(text)
    return {
        "since": added.group(1) if added else None,
        "deprecated": bool(deprecated),
        "deprecated_since": deprecated_since,
        "deprecated_note": note,
    }


def _common(node: ast.AST, name: str, module: str, qualified: str, file: Path) -> dict:
    raw = ast.get_docstring(node) if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef)) else None
    return {
        "name": name,
        "origin": "python",
        # Stable symbol ID for the docs graph: the dotted path, language-tagged.
        # Unlike C++ USRs (one per overload), Python overloads share the dotted
        # id — it is the logical symbol a `Type/member` link resolves to.
        "symbol_id": "py:" + qualified,
        "py_name": name,
        "hidden": False,
        "qualified_name": qualified,
        "doc": raw or "",
        "doc_structured": doc_structured(strip_availability_directives(raw)),
        "availability": availability(node, raw),
        "location": _location(file, node),
        "submodule": module,
        "directives": [],
    }


def decorator_names(node) -> list[str]:
    """Last component of each decorator: ``property``, ``setter``,
    ``staticmethod``, ``classmethod``, ``overload``, ``cached_property``, ...
    Handles attribute (``x.setter``, ``typing.overload``) and call
    (``functools.lru_cache()``) decorators alike."""
    out: list[str] = []
    for d in node.decorator_list:
        target = d.func if isinstance(d, ast.Call) else d
        out.append(ast.unparse(target).split(".")[-1])
    return out


def _callable_core(node, module: str, file: Path, qualprefix: str, *, drop_first: bool) -> dict:
    ent = _common(node, node.name, module, f"{qualprefix}.{node.name}", file)
    ent.update({
        "return_type": "",
        "return_type_canonical": "",
        # Return annotation recovered as source; resolved by the renderer's
        # KNOWN_TYPES path so a return naming a documented class cross-links.
        "return_py_type": _unparse(node.returns) or "",
        "params": params_from_args(node.args, drop_first=drop_first),
    })
    return ent


def function_entity(node, module: str, file: Path, *, qualprefix: str) -> dict:
    ent = _callable_core(node, module, file, qualprefix, drop_first=False)
    # Binding-centric fields a Python function never has — kept empty so no
    # consumer assumes they are present.
    ent.update({
        "is_template": False,
        "template_params": [],
        "template_kwargs": [],
        "instantiations": [],
        "python_overloads": [],
    })
    return ent


def method_entity(node, module: str, file: Path, *, qualprefix: str, is_static: bool, drop_first: bool) -> dict:
    ent = _callable_core(node, module, file, qualprefix, drop_first=drop_first)
    ent.update({
        "is_const": False,
        "is_static": is_static,
        "is_virtual": False,
        "is_pure_virtual": False,
        "is_constructor": node.name == "__init__",
        "is_destructor": False,
        "is_operator": False,
        "is_deleted": False,
        "template_params": [],
        "python_overloads": [],
    })
    return ent


def property_entity(node, module: str, file: Path, qualprefix: str) -> dict:
    """A @property getter -> BoundProperty shape (json_property)."""
    raw = ast.get_docstring(node)
    return {
        "py_name": node.name,
        "type": "",
        "py_type": _unparse(node.returns) or "",
        "doc": raw or "",
        "doc_structured": doc_structured(strip_availability_directives(raw)),
        "availability": availability(node, raw),
        "writable": False,
    }


def _group_by_name(nodes: list) -> tuple[dict[str, list], list[str]]:
    by_name: dict[str, list] = {}
    order: list[str] = []
    for n in nodes:
        if n.name not in by_name:
            by_name[n.name] = []
            order.append(n.name)
        by_name[n.name].append(n)
    return by_name, order


def _apply_overload_doc(entities: list[dict], defs: list, impl) -> None:
    """When @overload stubs lack their own docstring, borrow the implementation's
    so the rendered overload set is documented from the single source."""
    impl_raw = ast.get_docstring(impl) if impl is not None else None
    if not impl_raw:
        return
    for ent in entities:
        if not ent["doc"]:
            ent["doc"] = impl_raw
            ent["doc_structured"] = doc_structured(impl_raw)


def collect_functions(nodes: list, module: str, file: Path, qualprefix: str) -> list[dict]:
    """Build free-function entities, routing @overload sets correctly.

    A name with @overload defs contributes ONE entity per overload signature
    (sharing the py_name so the renderer groups them into a multi-signature
    directive); the un-decorated implementation supplies the docstring but is not
    rendered as its own signature."""
    by_name, order = _group_by_name(nodes)
    out: list[dict] = []
    for name in order:
        defs = by_name[name]
        overloads = [d for d in defs if "overload" in decorator_names(d)]
        if overloads:
            impl = next((d for d in defs if "overload" not in decorator_names(d)), None)
            ents = [function_entity(d, module, file, qualprefix=qualprefix) for d in overloads]
            _apply_overload_doc(ents, defs, impl)
            out += ents
        else:
            out += [function_entity(d, module, file, qualprefix=qualprefix) for d in defs]
    return out


def collect_methods(nodes: list, module: str, file: Path, qualprefix: str) -> list[dict]:
    """Build method entities (overload-aware). @staticmethod sets is_static and
    keeps the first arg; @classmethod drops the leading ``cls``."""
    def build(d) -> dict:
        decs = decorator_names(d)
        is_static = "staticmethod" in decs
        return method_entity(d, module, file, qualprefix=qualprefix, is_static=is_static, drop_first=not is_static)

    by_name, order = _group_by_name(nodes)
    out: list[dict] = []
    for name in order:
        defs = by_name[name]
        overloads = [d for d in defs if "overload" in decorator_names(d)]
        if overloads:
            impl = next((d for d in defs if "overload" not in decorator_names(d)), None)
            ents = [build(d) for d in overloads]
            _apply_overload_doc(ents, defs, impl)
            out += ents
        else:
            out += [build(d) for d in defs]
    return out


_ENUM_BASES = {"Enum", "IntEnum", "IntFlag", "Flag", "StrEnum", "ReprEnum"}


def is_enum(node: ast.ClassDef) -> bool:
    """True when a class derives from a stdlib ``enum`` base (by last name)."""
    return any(ast.unparse(b).split(".")[-1] in _ENUM_BASES for b in node.bases)


def _apply_data_doc(ent: dict, node: ast.AST) -> None:
    """Attach a data assignment's #: / # doc comment, if any."""
    doc = data_doc(node)
    if doc:
        ent["doc"] = doc
        ent["doc_structured"] = doc_structured(doc)


def field_entity(name: str, annotation: str, node: ast.AST, module: str, file: Path, qualprefix: str) -> dict:
    """A class-level data attribute (annotated assignment) -> BoundField shape."""
    ent = _common(node, name, module, f"{qualprefix}.{name}", file)
    ent.update({"type": "", "py_type": annotation or "", "is_static": True})
    _apply_data_doc(ent, node)
    return ent


# Value expressions simple enough to show verbatim for a module constant.
_SIMPLE_VALUE = (ast.Constant, ast.Tuple, ast.List, ast.Set, ast.Dict, ast.UnaryOp, ast.Name)


def variable_entity(name: str, annotation: str | None, value: ast.AST | None,
                    node: ast.AST, module: str, file: Path) -> dict:
    """A module-level constant -> a top-level ``variables`` (py:data) entity."""
    ent = _common(node, name, module, f"{module}.{name}", file)
    ent.update({
        "py_type": annotation or "",
        "value": _unparse(value) if isinstance(value, _SIMPLE_VALUE) else None,
    })
    _apply_data_doc(ent, node)
    return ent


def enum_entity(node: ast.ClassDef, module: str, file: Path, qualprefix: str) -> dict:
    """A Python ``enum.Enum`` subclass -> BoundEnum shape (with enumerators)."""
    qualified = f"{qualprefix}.{node.name}"
    ent = _common(node, node.name, module, qualified, file)
    enumerators: list[dict] = []
    for stmt in node.body:
        target = value = None
        if isinstance(stmt, ast.Assign) and len(stmt.targets) == 1 and isinstance(stmt.targets[0], ast.Name):
            target, value = stmt.targets[0].id, stmt.value
        elif isinstance(stmt, ast.AnnAssign) and isinstance(stmt.target, ast.Name):
            target, value = stmt.target.id, stmt.value
        if target is None or not is_public(target):
            continue
        # A real int literal -> its value; anything else (auto(), str, expr) ->
        # None, so the renderer doesn't claim a misleading ``0``.
        ival = value.value if isinstance(value, ast.Constant) and isinstance(value.value, int) else None
        edoc = data_doc(stmt)
        enumerators.append({"name": target, "value": ival, "doc": edoc, "doc_structured": doc_structured(edoc)})
    ent.update({
        "is_scoped": True,
        "underlying_type": "",
        "underlying_py_type": "int",
        "enumerators": enumerators,
    })
    return ent


def class_entity(node: ast.ClassDef, module: str, file: Path, *, qualprefix: str) -> dict:
    qualified = f"{qualprefix}.{node.name}"
    ent = _common(node, node.name, module, qualified, file)

    properties: dict[str, dict] = {}
    prop_order: list[str] = []
    method_defs: list = []
    fields: list[dict] = []
    nested_classes: list[dict] = []
    nested_enums: list[dict] = []
    for child in node.body:
        if isinstance(child, ast.ClassDef) and is_public(child.name):
            if is_enum(child):
                nested_enums.append(enum_entity(child, module, file, qualified))
            else:
                nested_classes.append(class_entity(child, module, file, qualprefix=qualified))
        elif isinstance(child, ast.AnnAssign) and isinstance(child.target, ast.Name) and is_public(child.target.id):
            # A class-level annotated data attribute (e.g. a dataclass field).
            fields.append(field_entity(child.target.id, _unparse(child.annotation), child, module, file, qualified))
        elif isinstance(child, (ast.FunctionDef, ast.AsyncFunctionDef)) and is_public(child.name):
            decs = decorator_names(child)
            if "property" in decs or "cached_property" in decs:
                if child.name not in properties:
                    prop_order.append(child.name)
                properties[child.name] = property_entity(child, module, file, qualified)
            elif "setter" in decs:
                # @<name>.setter — the property name is the method name.
                if child.name in properties:
                    properties[child.name]["writable"] = True
                else:
                    prop_order.append(child.name)
                    p = property_entity(child, module, file, qualified)
                    p["writable"] = True
                    properties[child.name] = p
            else:
                method_defs.append(child)

    built = collect_methods(method_defs, module, file, qualified)
    ctors = [m for m in built if m["name"] == "__init__"]
    methods = [m for m in built if m["name"] != "__init__"]

    ent.update({
        "is_template": False,
        "is_external": False,
        "template_params": [],
        "bases": [ast.unparse(b) for b in node.bases],
        "instantiations": [],
        "constructors": ctors,
        "methods": methods,
        "properties": [properties[n] for n in prop_order],
        "fields": fields,
        "enums": nested_enums,
        "nested_classes": nested_classes,
    })
    return ent


# ── File / package walking ───────────────────────────────────────────────────


def is_public(name: str) -> bool:
    """Documentable names (no ``__all__``): skip private (leading underscore)
    unless a dunder."""
    return not name.startswith("_") or (name.startswith("__") and name.endswith("__"))


def _string_list(value: ast.AST) -> set[str]:
    if isinstance(value, (ast.List, ast.Tuple, ast.Set)):
        return {e.value for e in value.elts if isinstance(e, ast.Constant) and isinstance(e.value, str)}
    return set()


def module_all(tree: ast.Module) -> set[str] | None:
    """The module's ``__all__`` set, or None when it declares none.

    ``__all__`` is the authoritative public surface. When present, only the
    names it lists are documented. Re-exports (``from .linalg import solve``)
    that appear in ``__all__`` are ALIASES — they resolve to the node already
    documented at its definition site (``einsums.linalg.solve``), so this
    frontend emits NOTHING for them rather than minting a second
    ``einsums.solve`` and double-documenting. (Only top-level FunctionDef /
    ClassDef yield entities here; an ImportFrom never does.)
    """
    for node in tree.body:
        if isinstance(node, ast.Assign):
            for t in node.targets:
                if isinstance(t, ast.Name) and t.id == "__all__":
                    return _string_list(node.value)
        elif isinstance(node, ast.AnnAssign) and isinstance(node.target, ast.Name) and node.target.id == "__all__":
            return _string_list(node.value) if node.value is not None else set()
    return None


def extract_file(file: Path, package: str, package_dir: Path,
                 *, source_root: Path | None = None) -> tuple[list[dict], list[dict], list[dict]]:
    """Return (classes, functions, enums) for one ``.py`` file."""
    module = dotted_module(file, package, package_dir)
    recorded = _recorded_path(file, source_root)
    source = file.read_text()
    tree = ast.parse(source, filename=str(file))
    # Index this file's comments so data assignments can pick up #: / # docs.
    _CTX["comments"], _CTX["code_rows"] = index_comments(source)

    allow = module_all(tree)
    public = (lambda name: name in allow) if allow is not None else is_public

    classes: list[dict] = []
    enums: list[dict] = []
    for n in tree.body:
        if isinstance(n, ast.ClassDef) and public(n.name):
            if is_enum(n):
                enums.append(enum_entity(n, module, recorded, module))
            else:
                classes.append(class_entity(n, module, recorded, qualprefix=module))
    func_defs = [
        n for n in tree.body
        if isinstance(n, (ast.FunctionDef, ast.AsyncFunctionDef)) and public(n.name)
    ]
    functions = collect_functions(func_defs, module, recorded, module)

    # Module-level constants -> variables (py:data). ``__all__`` is config, not data.
    variables: list[dict] = []
    for n in tree.body:
        target = ann = val = None
        if isinstance(n, ast.AnnAssign) and isinstance(n.target, ast.Name):
            target, ann, val = n.target.id, _unparse(n.annotation), n.value
        elif isinstance(n, ast.Assign) and len(n.targets) == 1 and isinstance(n.targets[0], ast.Name):
            target, val = n.targets[0].id, n.value
        if target is None or target == "__all__" or not public(target):
            continue
        variables.append(variable_entity(target, ann, val, n, module, recorded))

    return classes, functions, enums, variables


def class_edges(classes: list[dict]) -> list[dict]:
    """Relationship edges for the docs graph, mirroring the C++ frontend.

    `memberOf` (method/ctor -> class) and `inheritsFrom` (class -> each base).
    Bases are emitted as written names (Python can't statically resolve them to
    a symbol id); Phase 2's resolver matches them against the merged graph.
    `overrides` is omitted — it can't be determined statically without MRO."""
    edges: list[dict] = []

    def walk(cls: dict) -> None:
        cid = cls.get("symbol_id")
        for base in cls.get("bases", []):
            if cid:
                edges.append({"source": cid, "target": base, "kind": "inheritsFrom"})
        for m in (cls.get("constructors", []) + cls.get("methods", []) + cls.get("fields", []) + cls.get("enums", [])):
            if cid and m.get("symbol_id"):
                edges.append({"source": m["symbol_id"], "target": cid, "kind": "memberOf"})
        for nested in cls.get("nested_classes", []):
            if cid and nested.get("symbol_id"):
                edges.append({"source": nested["symbol_id"], "target": cid, "kind": "memberOf"})
            walk(nested)

    for c in classes:
        walk(c)
    return edges


def iter_py_files(package_dir: Path) -> list[Path]:
    return sorted(p for p in package_dir.rglob("*.py"))


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--package", required=True, help="top-level package import name (recorded as the document 'module')")
    ap.add_argument("--package-dir", required=True, help="filesystem directory of the package (contains __init__.py)")
    ap.add_argument("--output", "-o", default="-", help="path to write the docs-JSON fragment (default: stdout)")
    ap.add_argument("--source-root", default=None,
                    help="record location.file relative to this directory (e.g. the repo root) so the "
                         "renderer can build clean source links; default: record paths as given")
    ap.add_argument("files", nargs="*", help="specific .py files (default: every .py under --package-dir)")
    args = ap.parse_args()

    package_dir = Path(args.package_dir)
    source_root = Path(args.source_root) if args.source_root else None
    files = [Path(f) for f in args.files] if args.files else iter_py_files(package_dir)

    classes: list[dict] = []
    functions: list[dict] = []
    enums: list[dict] = []
    variables: list[dict] = []
    skipped = 0
    for f in files:
        # One unparseable file (a syntax error, or syntax newer than the running
        # Python) must not take down the whole package's docs — skip it, warn,
        # and keep going.
        try:
            c, fn, en, var = extract_file(f, args.package, package_dir, source_root=source_root)
        except (SyntaxError, ValueError) as e:
            log(f"warning: skipping {f}: {e.__class__.__name__}: {e}")
            skipped += 1
            continue
        except OSError as e:
            log(f"warning: cannot read {f}: {e}")
            skipped += 1
            continue
        classes.extend(c)
        functions.extend(fn)
        enums.extend(en)
        variables.extend(var)

    doc = {
        "schema_version": SCHEMA_VERSION,
        "module": args.package,
        "classes": classes,
        "functions": functions,
        "enums": enums,
        "typedefs": [],
        "concepts": [],
        "macros": [],
        "variables": variables,
        "edges": class_edges(classes),
    }

    text = json.dumps(doc, indent=2) + "\n"
    if args.output == "-":
        sys.stdout.write(text)
    else:
        Path(args.output).write_text(text)
        skipped_note = f", skipped {skipped} unparseable file(s)" if skipped else ""
        log(f"wrote {args.output} ({len(classes)} classes, {len(functions)} functions, "
            f"{len(enums)} enums, {len(variables)} variables) from {len(files)} file(s){skipped_note}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
