#!/usr/bin/env python3
# ----------------------------------------------------------------------------------------------
# Copyright (c) The Einsums Developers. All rights reserved.
# Licensed under the MIT License. See LICENSE.txt in the project root for license information.
# ----------------------------------------------------------------------------------------------
"""Generate the C++ API reference pages for selected modules (Option 2).

Each selected ``lib/module`` is parsed once by ``apiary --emit-cpp-docs-json``,
through a generated umbrella that includes every one of its headers, and the
modules are generated in parallel (``--jobs``). Then reStructuredText is
rendered in one of two layouts:

* ``--layout header`` (default): one page per header via
  ``apiary_render_cpp_rst.py --embed``, replacing Breathe's
  ``autodoxygenfile``. Unchanged legacy behavior.
* ``--layout entity``: one page per entity via
  ``apiary_render_cpp_site.py`` — a page per class/concept/function
  overload set plus per-module catch-alls and index pages, and a global
  landing page — written under ``<out-dir>/rst/<lib>/<module>/``. A
  ``.site.manifest`` prunes pages whose entity disappeared.

Compile flags are taken from a representative ``apiary`` codegen
command already present in the build's ``build.ninja`` (the Tensor module's,
whose transitive include set covers the whole library) — so we don't
re-derive per-module flags here.

Usage::

    apiary_gen_cpp_docs.py --source-dir <repo> --build-dir <build> --tool <apiary> \
                    --out-dir <dir> --modules Einsums/BLASVendor Einsums/Concepts \
                    [--layout entity --index-label-template "modules_{lib}_{module}_api" \
                     --backlink-label-template "modules_{lib}_{module}"]
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass, field
from pathlib import Path

from apiary_io import write_if_changed

SCRIPTS = Path(__file__).resolve().parent


def log(msg: str) -> None:
    print(f"gen_cpp_docs: {msg}", file=sys.stderr)


def universal_flags(build_dir: Path, source_dir: Path) -> list[str]:
    """Compile flags for parsing any module's headers.

    Start from a representative apiary command in build.ninja (it
    carries the resource-dir / isysroot / -std / system flags libtooling
    needs), then append EVERY module's include dir — both source and
    build-tree (for generated ``Defines.hpp``). The Tensor command alone
    only covers Tensor's transitive deps, so headers of modules Tensor
    doesn't depend on (ComputeGraph, Comm, GPU, ...) wouldn't resolve their
    own includes and would parse to nothing."""
    # Reading the build graph directly ties this to the Ninja generator. Say so
    # plainly rather than surfacing a bare FileNotFoundError, which under a
    # Visual Studio or Makefile build looks like a missing build dir.
    ninja_file = build_dir / "build.ninja"
    if not ninja_file.is_file():
        raise SystemExit(f"gen_cpp_docs: {ninja_file} not found - flags are read from the Ninja "
                         "build graph, so the build must be configured with -G Ninja")
    ninja = ninja_file.read_text(encoding="utf-8")
    # apiary is invoked through the ApiaryRun.cmake wrapper, so its argv reaches
    # build.ninja as a semicolon-joined CMake list: -DAPIARY_COMMAND=<exe>;<arg>;...
    # (ApiaryRun guarantees no argument contains a semicolon). Splitting on ';'
    # gives clean tokens and preserves Windows backslashes verbatim. Fall back to
    # the older space-separated direct invocation for builds that don't wrap.
    m = re.search(r'-DAPIARY_COMMAND=([^"\n]*apiary_register_Tensor[^"\n]*)', ninja)
    if m:
        toks = m.group(1).split(";")
    else:
        m = re.search(r"apiary --register-function apiary_register_Tensor [^\n]*", ninja)
        if not m:
            raise SystemExit("gen_cpp_docs: no Tensor pybind command in build.ninja (need EINSUMS_BUILD_PYTHON)")
        # POSIX lexing treats the backslashes in a Windows path as escapes and
        # eats them, silently corrupting every -I flag. Non-POSIX mode preserves
        # them but leaves quotes attached to the token, so strip those back off.
        toks = shlex.split(m.group(0), posix=(os.name != "nt"))
        if os.name == "nt":
            toks = [t.strip('"') for t in toks]
    if "--" not in toks:
        raise SystemExit("gen_cpp_docs: Tensor pybind command has no '--' compile-flags separator")
    flags = toks[toks.index("--"):]
    seen = {f for f in flags if f.startswith("-I")}
    for base in (source_dir / "libs", build_dir / "libs"):
        for inc in sorted(base.glob("*/*/include")):
            flag = f"-I{inc}"
            if flag not in seen:
                seen.add(flag)
                flags.append(flag)
    return flags


def sanitized(relheader: str) -> str:
    return relheader.replace("/", "_").replace(".", "_")


def header_relpath(header: Path) -> str | None:
    """The include-relative path (after ``include/``) — what --source-include
    and Breathe's autodoxygenfile key on."""
    parts = header.parts
    if "include" not in parts:
        return None
    return "/".join(parts[parts.index("include") + 1:])


def collect_template_params(doc: dict, out: set[str]) -> None:
    """Every template-parameter name anywhere in the document. These are
    never cross-reference targets, so the docs build nitpick-ignores them."""
    def from_decls(decls: list[dict]) -> None:
        # The parameters of a template template parameter are named in its
        # declaration (``template <typename Elem> typename C``) but are not
        # in the entity's flat ``template_params``.
        for tp in decls or []:
            if tp.get("name"):
                out.add(tp["name"])
            from_decls(tp.get("template_param_decls"))

    def from_entity(e: dict) -> None:
        out.update(e.get("template_params", []) or [])
        from_decls(e.get("template_param_decls"))

    def from_callable(c: dict) -> None:
        from_entity(c)

    def from_class(cl: dict) -> None:
        from_entity(cl)
        for m in cl.get("methods", []) + cl.get("constructors", []):
            from_callable(m)
        for n in cl.get("nested_classes", []):
            from_class(n)

    for cl in doc.get("classes", []):
        from_class(cl)
    for fn in doc.get("functions", []):
        from_callable(fn)
    for td in doc.get("typedefs", []):
        from_entity(td)
    for c in doc.get("concepts", []):
        from_entity(c)


# The per-entity lists of a docs document. Every entry carries a
# ``location``, which is how a module's document splits back into headers.
_ENTITY_KINDS = ("classes", "functions", "enums", "typedefs", "concepts", "macros", "variables")

# The member lists an entity can nest; walked to collect the symbol IDs a
# header owns, so its share of the top-level ``edges`` goes with it.
_MEMBER_KINDS = ("constructors", "methods", "fields", "properties", "enums", "nested_classes", "enumerators")


# A clang error diagnostic: ``path:line:col: error: ...`` (or ``fatal error``).
_CLANG_ERROR = re.compile(r":\d+:\d+: (?:fatal )?error: ")


@dataclass
class ApiaryRun:
    """What one apiary invocation produced: its docs JSON (empty on failure)
    and the report lines it printed."""
    stdout: str
    undoc: set[str] = field(default_factory=set)
    undoc_refs: set[str] = field(default_factory=set)
    # Whether clang reported an error. apiary still exits 0 and emits what it
    # could parse, so this is the only sign that declarations may be missing.
    clang_error: bool = False


def run_apiary(tool: str, flags: list[str], source: Path, relheaders: list[str],
               report_undoc: bool, report_refs: bool) -> ApiaryRun:
    """Run ``apiary --emit-cpp-docs-json`` over one source file, documenting
    the declarations of the headers in ``relheaders`` (include-relative)."""
    cmd = [tool, "--emit-cpp-docs-json", "--module", "einsums"]
    if report_undoc:
        cmd.append("--report-undocumented")
    if report_refs:
        cmd.append("--report-undocumented-references")
    for rel in relheaders:
        cmd += ["--source-include", rel]
    cmd += [str(source), *flags]
    res = subprocess.run(cmd, capture_output=True, text=True)
    run = ApiaryRun(res.stdout)
    # The tool prints "file:line:col: undocumented <kind> <name>" to stderr
    # (mixed with clang include-trace noise, which we drop), with a trailing
    # "referenced by <entity>" for the references report.
    for ln in res.stderr.splitlines():
        if ": undocumented " in ln:
            (run.undoc_refs if " referenced by " in ln else run.undoc).add(ln.strip())
        elif _CLANG_ERROR.search(ln):
            run.clang_error = True
    return run


def write_umbrella(path: Path, relheaders: list[str]) -> None:
    """A translation unit including every header of a module, so the module
    parses once instead of once per header. A project's own umbrella header
    will not do: it includes the headers users should reach for, and the
    reference documents every one."""
    lines = ["// Generated by apiary_gen_cpp_docs.py: every header of one module, parsed once.",
             *(f"#include <{rel}>" for rel in relheaders), ""]
    write_if_changed(path, "\n".join(lines))


def _symbol_ids(entity: dict, out: set[str]) -> None:
    if entity.get("symbol_id"):
        out.add(entity["symbol_id"])
    for kind in _MEMBER_KINDS:
        for member in entity.get(kind, []) or []:
            if isinstance(member, dict):
                _symbol_ids(member, out)


def split_by_header(doc: dict, headers: dict[Path, str]) -> dict[str, dict]:
    """Split a module's docs document into one document per header, keyed by
    include-relative path, by where each entity is declared. ``headers`` maps
    each header's resolved path to that key. A header that declares nothing
    still gets an (empty) document, as it did when it was parsed alone."""
    per: dict[str, dict] = {}
    for rel in headers.values():
        per[rel] = {k: ([] if isinstance(v, list) else v) for k, v in doc.items()}
    owned: dict[str, set[str]] = {rel: set() for rel in headers.values()}
    for kind in _ENTITY_KINDS:
        for entity in doc.get(kind, []) or []:
            file = (entity.get("location") or {}).get("file")
            rel = headers.get(Path(file).resolve()) if file else None
            if rel is None:
                continue
            per[rel][kind].append(entity)
            _symbol_ids(entity, owned[rel])
    for edge in doc.get("edges", []) or []:
        for rel, ids in owned.items():
            if edge.get("source") in ids:
                per[rel]["edges"].append(edge)
    return per


def render_header_page(json_path: Path, relheader: str, rst_out: Path) -> bool:
    """Render one header's page (``--layout header``)."""
    render = subprocess.run(
        [sys.executable, str(SCRIPTS / "apiary_render_cpp_rst.py"), str(json_path),
         "--title", relheader, "--output", str(rst_out), "--embed"],
        capture_output=True, text=True)
    if render.returncode != 0:
        log(f"render failed for {relheader}: {render.stderr.strip()[:200]}")
        return False
    return True


@dataclass
class ModuleResult:
    """One module's outputs, merged into the run's totals by main()."""
    lib: str
    module: str
    jsons: list[Path] = field(default_factory=list)
    pages: int = 0
    tparams: set[str] = field(default_factory=set)
    undoc: set[str] = field(default_factory=set)
    undoc_refs: set[str] = field(default_factory=set)
    note: str = ""


def gen_module(tool: str, flags: list[str], lib: str, module: str, inc: Path, out_dir: Path,
               entity_layout: bool, report_undoc: bool, report_refs: bool) -> ModuleResult:
    """Parse one module's headers and write its JSON and (header layout) pages.

    The whole module is parsed once, through a generated umbrella. Parsing is
    nearly all of apiary's time, and a module's headers share most of what
    they include, so parsing them one at a time repeated that work per header.
    If the umbrella yields nothing (the headers do not compile together), the
    module falls back to one parse per header."""
    result = ModuleResult(lib, module)
    headers: dict[Path, str] = {}
    for header in sorted(inc.rglob("*.hpp")):
        rel = header_relpath(header)
        if rel is not None:
            headers[header.resolve()] = rel
    if not headers:
        return result
    rels = list(headers.values())

    def absorb(run: ApiaryRun) -> dict | None:
        result.undoc |= run.undoc
        result.undoc_refs |= run.undoc_refs
        if not run.stdout.strip():
            return None
        try:
            doc = json.loads(run.stdout)
        except json.JSONDecodeError:
            return None
        collect_template_params(doc, result.tparams)
        return doc

    umbrella = out_dir / "umbrellas" / f"{lib}_{module}.hpp"
    write_umbrella(umbrella, rels)
    whole = run_apiary(tool, flags, umbrella, rels, report_undoc, report_refs)
    # Headers that conflict when included together (a redefinition, a macro
    # one of them expects undefined) still produce output, minus whatever
    # clang could not parse. Only a clean parse is trusted to be complete.
    doc = None if whole.clang_error else absorb(whole)

    if doc is None:
        # One parse per header, the way every module used to be generated.
        result.note = "headers do not parse together; parsed one at a time"
        for path, rel in headers.items():
            run = run_apiary(tool, flags, path, [rel], report_undoc, report_refs)
            if absorb(run) is None:
                continue
            json_out = out_dir / (sanitized(rel) + ".json")
            write_if_changed(json_out, run.stdout)
            result.jsons.append(json_out)
            if entity_layout or render_header_page(json_out, rel, out_dir / (sanitized(rel) + ".rst")):
                result.pages += 1
        return result

    # One parse lists entities in translation-unit order, which follows the
    # include graph. Order them by header, then position, as one parse per
    # header did, so page order does not depend on which header includes
    # which.
    order = {rel: i for i, rel in enumerate(sorted(rels))}

    def position(entity: dict) -> tuple[int, int, int]:
        loc = entity.get("location") or {}
        file = loc.get("file")
        rel = headers.get(Path(file).resolve()) if file else None
        return (order.get(rel, len(order)), loc.get("line", 0), loc.get("column", 0))

    for kind in _ENTITY_KINDS:
        if isinstance(doc.get(kind), list):
            doc[kind].sort(key=position)

    if entity_layout:
        json_out = out_dir / f"{lib}_{module}.module.json"
        write_if_changed(json_out, run_text(doc))
        result.jsons.append(json_out)
        result.pages = len(rels)
        return result
    for rel, part in split_by_header(doc, headers).items():
        json_out = out_dir / (sanitized(rel) + ".json")
        write_if_changed(json_out, run_text(part))
        result.jsons.append(json_out)
        if render_header_page(json_out, rel, out_dir / (sanitized(rel) + ".rst")):
            result.pages += 1
    return result


def run_text(doc: dict) -> str:
    """A docs document as the JSON text written to disk."""
    return json.dumps(doc, indent=2, sort_keys=True) + "\n"


def render_module_site(lib: str, module: str, jsons: list[Path], rst_dir: Path,
                       index_label_tpl: str, backlink_label_tpl: str) -> None:
    """Render one module's per-entity pages under ``<rst-dir>/<lib>/<module>/``."""
    outdir = rst_dir / lib / module
    cmd = [sys.executable, str(SCRIPTS / "apiary_render_cpp_site.py"),
           "--outdir", str(outdir), "--module-title", module,
           "--index-label", index_label_tpl.format(lib=lib, module=module)]
    backlink = backlink_label_tpl.format(lib=lib, module=module) if backlink_label_tpl else ""
    if backlink:
        cmd += ["--backlink-label", backlink]
    cmd += [str(j) for j in jsons]
    res = subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode != 0:
        raise SystemExit(f"gen_cpp_docs: site render failed for {lib}/{module}: "
                         f"{res.stderr.strip()[:500]}")


def entity_counts(jsons: list[Path]) -> tuple[int, int]:
    """(classes+concepts, free functions) documented in a module, for the
    landing page's one-line brief."""
    classes = set()
    functions = set()
    for j in jsons:
        text = j.read_text(encoding="utf-8")
        if not text.strip():
            continue
        try:
            doc = json.loads(text)
        except json.JSONDecodeError:
            continue
        for cl in doc.get("classes", []):
            if not cl.get("is_external"):
                classes.add(cl.get("qualified_name") or cl["name"])
        for c in doc.get("concepts", []):
            classes.add(c.get("qualified_name") or c["name"])
        for fn in doc.get("functions", []):
            functions.add(fn.get("qualified_name") or fn["name"])
    return len(classes), len(functions)


def write_root_index(rst_dir: Path, modules: list[tuple[str, str, list[Path]]],
                     root_title: str, root_label: str, index_label_tpl: str) -> None:
    # Alphabetical regardless of the order the caller wired the modules in.
    modules = sorted(modules, key=lambda m: (m[0].lower(), m[1].lower()))
    ind = "   "
    out = [f".. _{root_label}:", "", "=" * len(root_title), root_title, "=" * len(root_title), "",
           ".. note::",
           f"{ind}Generated from the C++ headers by ``apiary --emit-cpp-docs-json``.",
           "",
           "The C++ API reference is organized per module.", "",
           "Modules", "-------", ""]
    for lib, module, jsons in modules:
        label = index_label_tpl.format(lib=lib, module=module)
        ntypes, nfuncs = entity_counts(jsons)
        parts = []
        if ntypes:
            parts.append(f"{ntypes} type{'s' if ntypes != 1 else ''}")
        if nfuncs:
            parts.append(f"{nfuncs} function{'s' if nfuncs != 1 else ''}")
        brief = ", ".join(parts) if parts else "no documented public API yet"
        out.append(f"- :ref:`{module} <{label}>` - {brief}")
    out += ["", ".. toctree::", f"{ind}:maxdepth: 1", f"{ind}:hidden:", ""]
    for lib, module, _ in modules:
        out.append(f"{ind}{lib}/{module}/index")
    out.append("")
    write_if_changed(rst_dir / "index.rst", "\n".join(out))


def prune_stale(rst_dir: Path, rendered: set[tuple[str, str]]) -> None:
    """Delete module directories a previous run rendered that this run did
    not (a module was dropped or renamed). Stale pages WITHIN a re-rendered
    module are cleared by the site renderer itself, which owns its
    directory."""
    for lib_dir in sorted(p for p in rst_dir.iterdir() if p.is_dir()):
        for mod_dir in sorted(p for p in lib_dir.iterdir() if p.is_dir()):
            if (lib_dir.name, mod_dir.name) not in rendered:
                shutil.rmtree(mod_dir)
        if not any(lib_dir.iterdir()):
            lib_dir.rmdir()


def prune_stale_json(out_dir: Path, written: set[Path]) -> None:
    """Delete docs JSON a previous run wrote that this one did not: a header
    or module that went away, or the per-header files of a module now parsed
    whole. Only this script's own file names are touched."""
    for path in out_dir.glob("*.json"):
        if path.name.endswith(("_hpp.json", ".module.json")) and path not in written:
            path.unlink()


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--source-dir", required=True)
    ap.add_argument("--build-dir", required=True)
    ap.add_argument("--tool", required=True, help="apiary binary")
    ap.add_argument("--out-dir", required=True)
    ap.add_argument("--modules", nargs="+", default=None,
                    help="lib/module pairs, e.g. Einsums/BLASVendor. When omitted, every "
                         "libs/<lib>/<module>/include directory is discovered automatically "
                         "(handy for a full --report-undocumented sweep without pasting the list).")
    ap.add_argument("--report-undocumented", action="store_true",
                    help="Also collect a deduplicated punch-list of public C++ entities missing a "
                         "doc comment. Prints the sorted list to stdout and writes it to "
                         "<out-dir>/undocumented.txt. Does not change the generated pages.")
    ap.add_argument("--report-undocumented-references", action="store_true",
                    help="Also collect the undocumented classes, enums, and concepts that documented "
                         "signatures name. Each is a reference with nothing to resolve to, which a "
                         "nitpicky Sphinx build rejects. Prints the sorted list to stdout and writes it "
                         "to <out-dir>/undocumented_references.txt. Does not change the generated pages.")
    ap.add_argument("--jobs", "-j", type=int, default=os.cpu_count() or 1,
                    help="Modules to generate at once (default: the CPU count).")
    ap.add_argument("--layout", choices=("header", "entity"), default="header",
                    help="'header': one page per header (legacy). 'entity': one page per "
                         "class/concept/function overload set under <out-dir>/rst/.")
    ap.add_argument("--index-label-template", default="modules_{lib}_{module}_api",
                    help="entity layout: Sphinx label for each module index page")
    ap.add_argument("--backlink-label-template", default="",
                    help="entity layout: label of each module's narrative page for a "
                         "'See ... for the narrative documentation' link; empty for none")
    ap.add_argument("--root-title", default="C++ API Reference",
                    help="entity layout: title of the landing page at <out-dir>/rst/index.rst")
    ap.add_argument("--root-label", default="api_cpp",
                    help="entity layout: Sphinx label of the landing page")
    args = ap.parse_args()

    source = Path(args.source_dir)
    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    flags = universal_flags(Path(args.build_dir), source)

    modules = args.modules
    if modules is None:
        # Auto-discover: every libs/<lib>/<module>/include directory.
        modules = sorted(f"{inc.parts[-3]}/{inc.parts[-2]}"
                         for inc in (source / "libs").glob("*/*/include"))
        log(f"auto-discovered {len(modules)} modules under {source / 'libs'}")

    entity_layout = args.layout == "entity"
    rst_dir = out_dir / "rst"
    site_modules: list[tuple[str, str, list[Path]]] = []
    total = 0
    tparams: set[str] = set()
    undoc: set[str] | None = set() if args.report_undocumented else None
    undoc_refs: set[str] | None = set() if args.report_undocumented_references else None
    jobs: list[tuple[str, str, Path]] = []
    for mod in modules:
        lib, module = mod.split("/", 1)
        inc = source / "libs" / lib / module / "include"
        if not inc.is_dir():
            log(f"skip {mod}: no include dir")
            continue
        jobs.append((lib, module, inc))

    # Modules are independent, and each is one parse plus its render, so they
    # run side by side. Results are merged in module order, so the output
    # does not depend on which finishes first.
    def module_job(lib: str, module: str, inc: Path) -> ModuleResult:
        res = gen_module(args.tool, flags, lib, module, inc, out_dir, entity_layout,
                         undoc is not None, undoc_refs is not None)
        if entity_layout:
            render_module_site(lib, module, res.jsons, rst_dir,
                               args.index_label_template, args.backlink_label_template)
        return res

    with ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
        futures = [pool.submit(module_job, lib, module, inc) for lib, module, inc in jobs]
        results = [f.result() for f in futures]

    written: set[Path] = set()
    for res in results:
        written.update(res.jsons)
        total += res.pages
        tparams |= res.tparams
        if undoc is not None:
            undoc |= res.undoc
        if undoc_refs is not None:
            undoc_refs |= res.undoc_refs
        if entity_layout:
            site_modules.append((res.lib, res.module, res.jsons))
        log(f"{res.lib}/{res.module}: generated pages" + (f" ({res.note})" if res.note else ""))
    prune_stale_json(out_dir, written)
    if entity_layout:
        write_root_index(rst_dir, site_modules, args.root_title, args.root_label,
                         args.index_label_template)
        prune_stale(rst_dir, {(lib, module) for lib, module, _ in site_modules})
    # The collected template-parameter names — the docs build adds these to
    # nitpick_ignore (they are never cpp cross-reference targets).
    write_if_changed(out_dir / "template_params.txt", "\n".join(sorted(tparams)) + "\n")
    log(f"documented {total} headers in {len(results)} modules + {len(tparams)} template-param names "
        f"into {out_dir}")
    if undoc is not None:
        report = "\n".join(sorted(undoc))
        write_if_changed(out_dir / "undocumented.txt", report + ("\n" if report else ""))
        if report:
            print(report)
        log(f"{len(undoc)} undocumented public entit{'y' if len(undoc) == 1 else 'ies'} "
            f"(written to {out_dir / 'undocumented.txt'})")
    if undoc_refs is not None:
        # Separate per-header runs can name different referrers for one
        # undocumented entity; list each entity once, with its first referrer.
        by_entity: dict[str, str] = {}
        for ln in sorted(undoc_refs):
            m = re.search(r": undocumented (\S+ '[^']*')", ln)
            by_entity.setdefault(m.group(1) if m else ln, ln)
        report = "\n".join(by_entity.values())
        write_if_changed(out_dir / "undocumented_references.txt", report + ("\n" if report else ""))
        if report:
            print(report)
        log(f"{len(by_entity)} undocumented entit{'y' if len(by_entity) == 1 else 'ies'} referenced "
            f"from documented signatures (written to {out_dir / 'undocumented_references.txt'})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
