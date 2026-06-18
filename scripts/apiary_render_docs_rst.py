#!/usr/bin/env python3
# ----------------------------------------------------------------------------------------------
# Copyright (c) The Einsums Developers. All rights reserved.
# Licensed under the MIT License. See LICENSE.txt in the project root for license information.
# ----------------------------------------------------------------------------------------------
"""Render the Python API reference (reStructuredText) from the merged docs JSON.

This is the pure renderer at the end of the docs pipeline::

    frontends -> *.docs.json fragments -> apiary_merge_docs_json.py -> apiary_render_docs_rst.py

The merge stage (``apiary_merge_docs_json.py``) has already concatenated every
frontend's fragments into ONE canonical document, dropped non-surface
entities, de-duplicated, and resolved cross-origin collisions. This script
only formats: it buckets the entities by Python submodule (``einsums``,
``einsums.linalg``, ``einsums.graph``, ...) and writes Sphinx ``.rst`` pages.
It does NOT re-derive naming, de-dupe, or resolve collisions — every entity
already carries a resolved ``py_name``, an ``origin``, and a ``location``.

Usage::

    apiary_render_docs_rst.py --outdir <dir> docs.json
    apiary_merge_docs_json.py -o - frag1.json frag2.json | apiary_render_docs_rst.py --outdir <dir> -

One ``.rst`` is written per submodule, plus an ``index.rst`` with a toctree.
The schema this consumes is documented in ``docs/docs_json_schema.md``.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

from apiary_curation import load_content
from apiary_docs_schema import DEFAULT_TOP, full_module, load_document
from apiary_docs_resolve import build_resolver, rewrite_links

# The docs-graph resolver, set in main(); rewrites ``[[Type/member]]`` author
# links in doc text into reST py-domain cross-references. None until built.
RESOLVER = None

# Per-type curation (class doc path -> TypeCuration), set in main() from the
# content dir. Empty when no --content-dir or no type files.
TYPE_CURATIONS: dict = {}


def md(text: str) -> str:
    """Apply docs-graph link rewriting to a doc-text fragment, if a resolver is
    configured. A no-op otherwise, so the renderer still works standalone."""
    return rewrite_links(text, RESOLVER) if RESOLVER is not None else (text or "")

LICENSE_HEADER = """..
    ----------------------------------------------------------------------------------------------
     Copyright (c) The Einsums Developers. All rights reserved.
     Licensed under the MIT License. See LICENSE.txt in the project root for license information.
    ----------------------------------------------------------------------------------------------
"""

# Directive body indentation. Matches the hand-written reference pages
# (4 spaces) so the generated pages sit alongside them without a style jar.
IND = "    "


def log(msg: str) -> None:
    print(f"render_docs_rst: {msg}", file=sys.stderr)


# ── Source links ─────────────────────────────────────────────────────────────
#
# Populated from CLI in main(): origin ("cpp"/"python") -> a URL template with
# {file} and {line} placeholders. Source links are synthesized HERE (not in the
# merge stage) because the link is a render-time string built from location{};
# the merge layer only carries location{file,line}. Origin-aware: C++ and Python
# sources can live in different repos, so each origin gets its own template.
SOURCE_URL_TEMPLATES: dict[str, str] = {}


def availability_lines(entity: dict, indent: str) -> list[str]:
    """reST for an entity's availability: a ``.. versionadded::`` for ``since``
    and a Deprecated admonition (with the migration note) for ``deprecated``.
    Both frontends populate the same shape, so the badges render identically."""
    a = entity.get("availability") or {}
    out: list[str] = []
    if a.get("since"):
        out += ["", f"{indent}.. versionadded:: {a['since']}", ""]
    if a.get("deprecated"):
        note = (a.get("deprecated_note") or "").strip()
        if a.get("deprecated_since"):
            # A known version -> the proper Sphinx ``.. deprecated:: <ver>``.
            out += ["", f"{indent}.. deprecated:: {a['deprecated_since']}"]
            if note:
                out += ["", f"{indent}{IND}{md(note)}"]
            out += [""]
        else:
            # No version -> a plain Deprecated admonition.
            out += ["", f"{indent}.. admonition:: Deprecated", f"{indent}{IND}:class: warning", ""]
            if note:
                out += [f"{indent}{IND}{md(note)}", ""]
    return out


def source_link_lines(entity: dict, indent: str) -> list[str]:
    """A reST ``[source]`` hyperlink for an entity, or [] when not configured.

    Picks the template by ``origin``; falls back to the ``location.file``
    extension (``.py`` -> python, else cpp) for entities that don't carry an
    origin field (e.g. C++ nested methods)."""
    if not SOURCE_URL_TEMPLATES:
        return []
    loc = entity.get("location") or {}
    file = loc.get("file")
    if not file:
        return []
    tmpl = SOURCE_URL_TEMPLATES.get(entity.get("origin"))
    if tmpl is None:
        tmpl = SOURCE_URL_TEMPLATES.get("python" if str(file).endswith(".py") else "cpp")
    if not tmpl:
        return []
    url = tmpl.format(file=file, line=loc.get("line") or 0)
    return ["", f"{indent}`[source] <{url}>`__", ""]


# ── Loading & grouping ──────────────────────────────────────────────────────
#
# The heavy lifting (de-dupe, hidden/external dropping, cross-origin collision
# resolution) happens upstream in apiary_merge_docs_json.py. This renderer expects an
# already-merged document, so grouping here is pure layout: bucket each entity
# onto its submodule page. The de-dupe/identity helpers live once in
# apiary_docs_schema.py.


def load_documents(paths: list[str]) -> tuple[str, list[dict]]:
    """Load the merged docs-JSON document(s), returning (top_module, documents).

    Normally a single already-merged file; accepting more than one simply
    buckets them together (no de-dupe — that is the merge stage's job)."""
    docs = [load_document(p, prefix="render_docs_rst") for p in paths]
    top_module = next((d.get("module") for d in docs if d.get("module")), DEFAULT_TOP)
    return top_module, docs


_PAGE_KINDS = ("classes", "functions", "enums", "variables")


def group_by_module(top: str, docs: list[dict]) -> dict[str, dict]:
    """Bucket classes/functions/enums/variables onto their submodule pages."""
    groups: dict[str, dict] = {}

    def bucket(mod: str) -> dict:
        return groups.setdefault(mod, {k: [] for k in _PAGE_KINDS})

    for doc in docs:
        for kind in _PAGE_KINDS:
            for ent in doc.get(kind, []):
                bucket(full_module(ent, top))[kind].append(ent)
    return groups


# ── Type-annotation sanitization ────────────────────────────────────────────
#
# The IR's py_type strings are a best-effort C++→Python translation. Where the
# translator can't resolve a type it hands back the C++ spelling (template
# parameters like ``AType``/``T``, dependent types like
# ``einsums::RuntimeTensorView<T>``, ``typename AType::ValueType``). Those are
# not valid/resolvable Python annotations, so Sphinx's nitpicky mode flags
# them. We post-process every annotation for *display*: keep builtins, typing
# containers, and documented class names; collapse everything unresolvable to
# ``Any``. (The .pyi path deliberately keeps the C++ spelling for pyright; the
# docs want a clean, resolvable annotation instead.)

# Populated in main() from the documented class/enum names so a class
# referenced in a signature renders as itself rather than ``Any``.
KNOWN_TYPES: set[str] = set()

_PY_BUILTIN_TYPES = {
    "int", "float", "str", "bool", "None", "complex", "bytes", "bytearray",
    "object", "Any", "memoryview", "slice", "Iterator", "Iterable", "Sequence",
    "Mapping", "Callable",
}
_PY_CONTAINERS = {"list", "tuple", "dict", "set", "frozenset", "Callable", "Sequence", "Iterable", "Mapping"}

_LEAF_RE = re.compile(r"^([A-Za-z_][\w.]*)\s*\[(.*)\]$", re.S)


def _split_top(s: str, sep: str) -> list[str]:
    """Split on `sep` at bracket/angle/paren depth 0."""
    out, cur, depth = [], [], 0
    for ch in s:
        if ch in "[<(":
            depth += 1
        elif ch in "]>)":
            depth -= 1
        if ch == sep and depth == 0:
            out.append("".join(cur))
            cur = []
        else:
            cur.append(ch)
    out.append("".join(cur))
    return [p.strip() for p in out]


def pythonize(ann: str) -> str:
    """Sanitize a single annotation string into a resolvable Python form."""
    s = (ann or "").strip()
    if not s:
        return "Any"
    # Union: A | B | C
    parts = _split_top(s, "|")
    if len(parts) > 1:
        return " | ".join(pythonize(p) for p in parts)
    # A bracketed list (e.g. Callable's [Args] group): [A, B] -> recurse each
    if s.startswith("[") and s.endswith("]"):
        return "[" + ", ".join(pythonize(a) for a in _split_top(s[1:-1], ",")) + "]"
    # Container head[...]
    m = _LEAF_RE.match(s)
    if m:
        head, args = m.group(1), m.group(2)
        if head.split(".")[-1] in _PY_CONTAINERS or head.startswith("numpy"):
            return f"{head}[" + ", ".join(pythonize(a) for a in _split_top(args, ",")) + "]"
        return "Any"  # unknown templated C++ type
    return _pythonize_leaf(s)


_STD_EXCEPTIONS = {
    "exception", "logic_error", "runtime_error", "invalid_argument", "out_of_range",
    "length_error", "domain_error", "range_error", "overflow_error", "underflow_error",
    "bad_alloc", "bad_cast", "system_error",
}


def _pythonize_leaf(s: str) -> str:
    s = s.replace("typename ", "").strip().rstrip("&* ").strip()
    s = re.sub(r"<.*>$", "", s).strip()      # drop template args
    if "::" in s:
        s = s.split("::")[-1]                # qualified C++ name -> last component
    if s in _STD_EXCEPTIONS:
        return "Exception"
    if s.startswith("numpy") or s in _PY_BUILTIN_TYPES or s in KNOWN_TYPES:
        return s
    return "Any"                              # template params / unresolved -> Any


# ── Signature & doc formatting ──────────────────────────────────────────────


def py_signature(params: list[dict]) -> str:
    """Render a Python parameter list from IR params (py-stub forms)."""
    parts = []
    for p in params:
        name = p.get("name") or "arg"
        ann = pythonize(p.get("py_type") or "")
        piece = f"{name}: {ann}" if ann else name
        default = p.get("default_py") or p.get("default")
        if default:
            piece += f" = {default}"
        parts.append(piece)
    return ", ".join(parts)


def emit_text(out: list[str], text: str, indent: str) -> None:
    """Emit a block of (already reST-ready) text at the given indent."""
    text = md((text or "").strip())
    if not text:
        return
    out.append("")
    for line in text.split("\n"):
        out.append(f"{indent}{line}".rstrip())
    out.append("")


def emit_doc(out: list[str], entity: dict, indent: str, *, with_params: bool) -> None:
    """Emit an entity's structured doc as reST under a directive.

    Renders ``brief`` then ``detail``; for callables also renders ``:param:``
    / ``:returns:`` / ``:raises:`` field lists. Falls back to the raw ``doc``
    string when no structured form is present (older JSON, or empty parse).
    """
    ds = entity.get("doc_structured") or {}
    brief = md((ds.get("brief") or "").strip())
    detail = md((ds.get("detail") or "").strip())
    params = ds.get("params", []) if with_params else []
    returns = md((ds.get("returns") or "").strip()) if with_params else ""
    throws = ds.get("throws", []) if with_params else []

    if not (brief or detail or params or returns or throws):
        emit_text(out, entity.get("doc", ""), indent)
        return

    out.append("")
    if brief:
        for line in brief.split("\n"):
            out.append(f"{indent}{line}".rstrip())
    if detail:
        out.append("")
        for line in detail.split("\n"):
            out.append(f"{indent}{line}".rstrip())
    if params or returns or throws:
        out.append("")
        for p in params:
            desc = md((p.get("description") or "").strip())
            out.append(f"{indent}:param {p['name']}: {desc}".rstrip())
        if returns:
            out.append(f"{indent}:returns: {returns}".rstrip())
        for t in throws:
            desc = md((t.get("description") or "").strip())
            out.append(f"{indent}:raises {pythonize(t['name'])}: {desc}".rstrip())
    out.append("")


# ── Per-entity rendering ────────────────────────────────────────────────────


def has_directive(entity: dict, name: str) -> bool:
    return any(d.get("name") == name for d in entity.get("directives", []))


def entity_has_doc(e: dict) -> bool:
    ds = e.get("doc_structured") or {}
    return bool(ds.get("brief") or ds.get("detail") or ds.get("params") or ds.get("returns") or ds.get("throws") or e.get("doc"))


def entity_signature_body(e: dict) -> str:
    """The ``(params) -> return`` tail of a callable's signature."""
    return f"({py_signature(e.get('params', []))}) -> {pythonize(e.get('return_py_type') or 'None')}"


def entity_py_names(e: dict, *, is_ctor: bool = False) -> list[str]:
    """Distinct Python names a callable binds under (overload set)."""
    if is_ctor:
        return ["__init__"]
    names = [o["py_name"] for o in e.get("python_overloads", []) if o.get("py_name")]
    if not names:
        names = [e.get("py_name") or e["name"]]
    return list(dict.fromkeys(names))


def emit_overload_set(out: list[str], base_indent: str, directive: str, name: str, members: list[dict]) -> None:
    """Emit one py-domain directive carrying every overload's signature.

    Grouping overloads (free-function overload sets, multiple constructors,
    overloaded methods) under a single directive — rather than repeating the
    directive — is both correct reST and avoids Sphinx "duplicate object
    description" warnings. Doc text comes from the first member that has one.
    """
    prefix = f"{base_indent}.. py:{directive}:: "
    pad = " " * len(prefix)
    sigs = list(dict.fromkeys(f"{name}{entity_signature_body(m)}" for m in members))
    out.append(prefix + sigs[0])
    for s in sigs[1:]:
        out.append(pad + s)
    doc_member = next((m for m in members if entity_has_doc(m)), members[0])
    emit_doc(out, doc_member, base_indent + IND, with_params=True)
    # Availability from any member of the set (overloads share a symbol).
    avail_member = next((m for m in members if m.get("availability")), doc_member)
    out.extend(availability_lines(avail_member, base_indent + IND))
    out.extend(source_link_lines(doc_member, base_indent + IND))
    out.append("")


def render_attribute(out: list[str], py_name: str, py_type: str, entity: dict, writable: bool) -> None:
    out.append(f"{IND}.. py:attribute:: {py_name}")
    if py_type:
        out.append(f"{IND}{IND}:type: {pythonize(py_type)}")
    emit_doc(out, entity, IND * 2, with_params=False)
    if not writable:
        # Plain text, not ``*...*`` — docutils rejects emphasis whose content
        # opens with '(' on its own line. A blank line MUST separate this body
        # line from the ``:type:`` option block above, otherwise docutils reads
        # it as a malformed option ("invalid option block").
        out.append("")
        out.append(f"{IND}{IND}(read-only)")
        out.append("")
    out.extend(availability_lines(entity, IND * 2))
    out.extend(source_link_lines(entity, IND * 2))


def class_py_names(cls: dict) -> list[str]:
    """The Python class name(s) this entity binds under.

    Templated classes contribute one Python class per instantiation
    (``RuntimeTensorF``, ``RuntimeTensorD``, ...); non-templated classes bind
    under their single resolved ``py_name``.
    """
    if cls.get("is_template"):
        names = [i["py_name"] for i in cls.get("instantiations", []) if i.get("py_name")]
        if names:
            return list(dict.fromkeys(names))
    return [cls.get("py_name") or cls["name"]]


def _method_directive(members: list[dict]) -> str:
    return "staticmethod" if members[0].get("is_static") else "method"


def render_class(out: list[str], cls: dict, name_prefix: str = "") -> None:
    # Per-type curation (a class's own ## Topics), keyed by the class doc path.
    type_cur = None
    if TYPE_CURATIONS and RESOLVER is not None and cls.get("symbol_id"):
        type_cur = TYPE_CURATIONS.get(RESOLVER.path_for_symbol(cls["symbol_id"]))

    for cname in class_py_names(cls):
        out.append(f".. py:class:: {name_prefix}{cname}")
        emit_doc(out, cls, IND, with_params=False)
        # Authored type overview, prepended to the class body.
        if type_cur is not None and type_cur.overview.strip():
            out.append("")
            for line in md(type_cur.overview).split("\n"):
                out.append(f"{IND}{line}".rstrip())
            out.append("")
        out.extend(availability_lines(cls, IND))
        out.extend(source_link_lines(cls, IND))
        out.append("")
        ctors = cls.get("constructors", [])
        if ctors:
            emit_overload_set(out, IND, "method", "__init__", ctors)
        # Group overloaded methods by Python name; skip hidden and the
        # getter/setter backing methods (exposed via their property).
        method_groups: dict[str, list[dict]] = {}
        for m in cls.get("methods", []):
            if m.get("hidden") or has_directive(m, "getter") or has_directive(m, "setter"):
                continue
            for name in entity_py_names(m):
                method_groups.setdefault(name, []).append(m)

        # Curated member topics first, each under a ``.. rubric::`` heading.
        used: set[str] = set()
        if type_cur is not None and type_cur.topics and RESOLVER is not None:
            for topic in type_cur.topics:
                names = []
                for token in topic.tokens:
                    entry = RESOLVER.resolve(token)
                    nm = entry.target.rsplit(".", 1)[-1] if entry else None
                    if nm in method_groups and nm not in used and nm not in names:
                        names.append(nm)
                if not names:
                    continue
                out.append(f"{IND}.. rubric:: {topic.title}")
                out.append("")
                for nm in names:
                    emit_overload_set(out, IND, _method_directive(method_groups[nm]), nm, method_groups[nm])
                    used.add(nm)

        for name, members in method_groups.items():
            if name in used:
                continue
            emit_overload_set(out, IND, _method_directive(members), name, members)
        for p in cls.get("properties", []):
            render_attribute(out, p["py_name"], p.get("py_type", ""), p, p.get("writable", False))
        for f in cls.get("fields", []):
            if f.get("hidden"):
                continue
            render_attribute(out, f.get("py_name") or f["name"], f.get("py_type", ""), f, True)
        for e in cls.get("enums", []):
            render_enum(out, e, nested_indent=IND)
        out.append("")
        # Nested classes render as qualified entries (``Outer.Inner``) after the
        # parent body, so Sphinx scopes them without indent threading.
        for nested in cls.get("nested_classes", []):
            render_class(out, nested, name_prefix=f"{name_prefix}{cname}.")


def render_enum(out: list[str], en: dict, nested_indent: str = "") -> None:
    name = en.get("py_name") or en["name"]
    out.append(f"{nested_indent}.. py:class:: {name}")
    emit_doc(out, en, nested_indent + IND, with_params=False)
    out.extend(availability_lines(en, nested_indent + IND))
    out.extend(source_link_lines(en, nested_indent + IND))
    out.append("")
    for v in en.get("enumerators", []):
        out.append(f"{nested_indent}{IND}.. py:attribute:: {v['name']}")
        if v.get("value") is not None:
            out.append(f"{nested_indent}{IND}{IND}:value: {v['value']}")
        emit_doc(out, v, nested_indent + IND * 2, with_params=False)
        out.append("")
    out.append("")


def render_variable(out: list[str], var: dict) -> None:
    out.append(f".. py:data:: {var.get('py_name') or var['name']}")
    if var.get("py_type"):
        out.append(f"{IND}:type: {pythonize(var['py_type'])}")
    if var.get("value"):
        out.append(f"{IND}:value: {var['value']}")
    emit_doc(out, var, IND, with_params=False)
    out.extend(availability_lines(var, IND))
    out.extend(source_link_lines(var, IND))
    out.append("")


# ── Page assembly ───────────────────────────────────────────────────────────


def _heading(out: list[str], text: str, char: str) -> None:
    out.append(text)
    out.append(char * max(len(text), 3))
    out.append("")


def _first_sentence(text: str) -> str:
    """First sentence of a (possibly multi-line) prose block, collapsed."""
    collapsed = " ".join((text or "").split())
    head, sep, _ = collapsed.partition(". ")
    return head + ("." if sep else "")


def brief_of(entity: dict) -> str:
    """The one-line brief of an entity, whitespace-collapsed (for summaries)."""
    ds = entity.get("doc_structured") or {}
    return " ".join((ds.get("brief") or "").split())


def render_summary(out: list[str], module: str, classes: list[dict],
                   func_groups: dict[str, list[dict]], enums: list[dict], variables: list[dict]) -> None:
    """A compact, link-rich Summary at the top of a module page — every symbol
    as a py-domain cross-reference plus its brief, so the page leads with a
    navigable index (DocC's per-page symbol listing)."""
    items: list[tuple[str, str, str]] = []  # (role, dotted-target, brief)
    for c in sorted(classes, key=lambda c: c.get("py_name") or c["name"]):
        for nm in class_py_names(c):
            items.append(("class", f"{module}.{nm}", brief_of(c)))
    for name in sorted(func_groups):
        members = func_groups[name]
        doc_member = next((m for m in members if brief_of(m)), members[0])
        items.append(("func", f"{module}.{name}", brief_of(doc_member)))
    for e in sorted(enums, key=lambda e: e.get("py_name") or e["name"]):
        items.append(("class", f"{module}.{e.get('py_name') or e['name']}", brief_of(e)))
    for v in sorted(variables, key=lambda v: v.get("py_name") or v["name"]):
        items.append(("data", f"{module}.{v.get('py_name') or v['name']}", brief_of(v)))
    if not items:
        return
    _heading(out, "Summary", "-")
    for role, target, brief in items:
        line = f"- :py:{role}:`~{target}`"
        if brief:
            line += f" — {md(brief)}"
        out.append(line)
    out.append("")


def render_by_kind(out: list[str], classes: list[dict], func_groups: dict[str, list[dict]],
                   func_names: list[str], enums: list[dict], variables: list[dict]) -> None:
    """Render a set of symbols under the default by-kind sections."""
    if classes:
        _heading(out, "Classes", "-")
        for c in sorted(classes, key=lambda c: c.get("py_name") or c["name"]):
            render_class(out, c)
    if func_names:
        _heading(out, "Functions", "-")
        for name in sorted(func_names):
            emit_overload_set(out, "", "function", name, func_groups[name])
    if enums:
        _heading(out, "Enumerations", "-")
        for e in sorted(enums, key=lambda e: e.get("py_name") or e["name"]):
            render_enum(out, e)
    if variables:
        _heading(out, "Data", "-")
        for v in sorted(variables, key=lambda v: v.get("py_name") or v["name"]):
            render_variable(out, v)


def render_page(module: str, group: dict, curation=None) -> str:
    out: list[str] = [LICENSE_HEADER, ""]
    # Namespace the label (``api_python_einsums_linalg``) so it never
    # collides with the hand-written pages' targets (e.g. ``_einsums``).
    label = "api_python_" + module.replace(".", "_")
    out.append(f".. _{label}:")
    out.append("")
    title = f"``{module}``"
    _heading_overline(out, title)
    out.append(".. note::")
    out.append(f"{IND}This page is generated by ``apiary`` from the binding")
    out.append(f"{IND}annotations and the package's Python source. Do not edit by hand.")
    out.append("")
    out.append(f".. py:module:: {module}")
    out.append("")

    # Authored module overview prose (``[[ ]]`` links resolved by md()).
    if curation is not None and curation.overview.strip():
        for line in md(curation.overview).split("\n"):
            out.append(line.rstrip())
        out.append("")

    classes = group["classes"]
    enums = group["enums"]
    variables = group["variables"]
    func_groups: dict[str, list[dict]] = {}
    for f in group["functions"]:
        for name in entity_py_names(f):
            func_groups.setdefault(name, []).append(f)

    cls_by_id = {c["symbol_id"]: c for c in classes if c.get("symbol_id")}
    enum_by_id = {e["symbol_id"]: e for e in enums if e.get("symbol_id")}
    var_by_id = {v["symbol_id"]: v for v in variables if v.get("symbol_id")}
    func_name_by_id = {f["symbol_id"]: name for name, fs in func_groups.items() for f in fs if f.get("symbol_id")}

    # Lead with a compact, link-rich summary of everything on the page.
    render_summary(out, module, classes, func_groups, enums, variables)

    used_cls: set[str] = set()
    used_enum: set[str] = set()
    used_func: set[str] = set()
    used_var: set[str] = set()

    # Curated topic groups first, in authored order.
    if curation is not None and curation.topics and RESOLVER is not None:
        for topic in curation.topics:
            _heading(out, topic.title, "-")
            for token in topic.tokens:
                entry = RESOLVER.resolve(token)
                sid = entry.symbol_id if entry else None
                if sid in cls_by_id and sid not in used_cls:
                    render_class(out, cls_by_id[sid])
                    used_cls.add(sid)
                elif sid in enum_by_id and sid not in used_enum:
                    render_enum(out, enum_by_id[sid])
                    used_enum.add(sid)
                elif sid in var_by_id and sid not in used_var:
                    render_variable(out, var_by_id[sid])
                    used_var.add(sid)
                elif sid in func_name_by_id and func_name_by_id[sid] not in used_func:
                    name = func_name_by_id[sid]
                    emit_overload_set(out, "", "function", name, func_groups[name])
                    used_func.add(name)

    # Everything left uncurated still renders, auto-grouped by kind, so nothing
    # silently disappears (DocC's fallback behavior).
    render_by_kind(
        out,
        [c for c in classes if c.get("symbol_id") not in used_cls],
        func_groups,
        [n for n in func_groups if n not in used_func],
        [e for e in enums if e.get("symbol_id") not in used_enum],
        [v for v in variables if v.get("symbol_id") not in used_var],
    )

    return "\n".join(out).rstrip() + "\n"


def _heading_overline(out: list[str], title: str) -> None:
    bar = "=" * len(title)
    out += [bar, title, bar, ""]


def render_article(article) -> str:
    out: list[str] = [LICENSE_HEADER, ""]
    out.append(f".. _api_article_{article.slug.replace('-', '_').replace('.', '_')}:")
    out.append("")
    _heading_overline(out, article.title)
    for line in md(article.body).split("\n"):
        out.append(line.rstrip())
    return "\n".join(out).rstrip() + "\n"


def render_index(modules: list[str], articles: list = (), module_briefs: dict | None = None) -> str:
    out: list[str] = [LICENSE_HEADER, ""]
    out.append(".. _api_python:")
    out.append("")
    title = "Python API Reference"
    _heading_overline(out, title)

    # An overview list of the modules: a link to each page plus a one-line
    # summary (its authored overview, else its symbol counts) — the API's
    # front door, above the navigation toctrees.
    briefs = module_briefs or {}
    if modules:
        _heading(out, "Modules", "-")
        for m in modules:
            line = f"- :doc:`{m}`"
            if briefs.get(m):
                line += f" — {md(briefs[m])}"
            out.append(line)
        out.append("")

    if articles:
        out.append(".. toctree::")
        out.append(f"{IND}:maxdepth: 1")
        out.append(f"{IND}:caption: Guides")
        out.append("")
        for a in articles:
            out.append(f"{IND}{a.slug}")
        out.append("")
    out.append(".. toctree::")
    out.append(f"{IND}:maxdepth: 2")
    out.append(f"{IND}:caption: API")
    out.append("")
    for m in modules:
        out.append(f"{IND}{m}")
    out.append("")
    return "\n".join(out).rstrip() + "\n"


def report_coverage(curations: dict, groups: dict, top: str) -> None:
    """Warn (to stderr) about curation gaps: a documented symbol in a curated
    module that no topic lists (it still renders, under a by-kind fallback), and
    a curated ``[[ ]]`` link that resolves to nothing."""
    for module, cur in curations.items():
        group = groups.get(module, {})
        documented: dict[str, str] = {}
        for kind in ("classes", "functions", "enums", "variables"):
            for e in group.get(kind, []):
                if e.get("symbol_id"):
                    documented[e["symbol_id"]] = e.get("py_name") or e.get("name", "")

        curated: set[str] = set()
        for token in cur.curated_tokens():
            entry = RESOLVER.resolve(token) if RESOLVER is not None else None
            if entry is None:
                log(f"coverage: {module}: curated link [[{token}]] resolves to nothing")
            else:
                curated.add(entry.symbol_id)

        for sid, name in sorted(documented.items(), key=lambda kv: kv[1]):
            if sid not in curated:
                log(f"coverage: {module}: '{name}' is documented but not curated "
                    f"(rendered under a by-kind fallback)")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("inputs", nargs="+", help="merged docs-JSON file (or '-' for stdin)")
    ap.add_argument("--outdir", required=True, help="directory to write .rst pages into")
    ap.add_argument("--cpp-source-url-template", default=None,
                    help="URL template for C++-origin source links, with {file} and {line} "
                         "placeholders, e.g. 'https://github.com/org/einsums/blob/main/{file}#L{line}'")
    ap.add_argument("--py-source-url-template", default=None,
                    help="URL template for Python-origin source links (same placeholders). C++ and "
                         "Python sources may live in different repos, hence separate templates.")
    ap.add_argument("--content-dir", default=None,
                    help="directory of authored Markdown: per-module curation (stem == dotted "
                         "module, with a ## Topics section) and free-standing articles. Optional.")
    args = ap.parse_args()

    if args.cpp_source_url_template:
        SOURCE_URL_TEMPLATES["cpp"] = args.cpp_source_url_template
    if args.py_source_url_template:
        SOURCE_URL_TEMPLATES["python"] = args.py_source_url_template

    top, docs = load_documents(args.inputs)
    groups = group_by_module(top, docs)
    if not groups:
        log("no entities found; nothing written")
        return 0

    # The docs-graph resolver powers ``[[ ]]`` link rewriting and seeds the type
    # sanitizer below, so a type named anywhere in the merged graph (including
    # another submodule) is recognized rather than collapsed to ``Any``.
    global RESOLVER
    RESOLVER = build_resolver(docs)

    # Record every documented class/enum name (including per-instantiation
    # class names) so the annotation sanitizer keeps real types and only
    # collapses genuinely-unresolvable ones (template params, dependent C++
    # spellings) to Any.
    KNOWN_TYPES.update(RESOLVER.class_and_enum_names())
    for g in groups.values():
        for c in g["classes"]:
            KNOWN_TYPES.update(class_py_names(c))
        for e in g["enums"]:
            KNOWN_TYPES.add(e.get("py_name") or e["name"])

    # Authored content (optional): per-module curation, per-type curation, and
    # free-standing articles.
    curations: dict = {}
    articles: list = []
    if args.content_dir:
        global TYPE_CURATIONS
        curations, TYPE_CURATIONS, articles = load_content(
            args.content_dir, set(groups), RESOLVER.class_paths())
        report_coverage(curations, groups, top)

    outdir = Path(args.outdir)
    outdir.mkdir(parents=True, exist_ok=True)

    # Top-level module first, then submodules alphabetically.
    modules = sorted(groups, key=lambda m: (m != top, m))
    module_briefs: dict[str, str] = {}
    for module in modules:
        g = groups[module]
        page = render_page(module, g, curations.get(module))
        (outdir / f"{module}.rst").write_text(page)
        log(f"wrote {module}.rst ({len(g['classes'])} classes, "
            f"{len(g['functions'])} functions, {len(g['enums'])} enums)")
        # Index summary line: the authored overview's first sentence, else counts.
        cur = curations.get(module)
        _counts = [(len(g["classes"]), "classes"), (len(g["functions"]), "functions"),
                   (len(g["enums"]), "enums"), (len(g["variables"]), "variables")]
        module_briefs[module] = _first_sentence(cur.overview if cur else "") or \
            ", ".join(f"{n} {label}" for n, label in _counts if n)

    for article in articles:
        (outdir / f"{article.slug}.rst").write_text(render_article(article))
        log(f"wrote article {article.slug}.rst")

    (outdir / "index.rst").write_text(render_index(modules, articles, module_briefs))
    log(f"wrote index.rst with {len(modules)} module(s), {len(articles)} article(s)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
