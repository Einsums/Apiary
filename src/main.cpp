//----------------------------------------------------------------------------------------------
// Copyright (c) The Einsums Developers. All rights reserved.
// Licensed under the MIT License. See LICENSE.txt in the project root for license information.
//----------------------------------------------------------------------------------------------

// apiary — Phase 3 driver.
//
// Walks the headers given on the command line, builds a Module IR, and
// emits a pybind11 binding TU. The output is post-clang-format using the
// project's .clang-format if reachable from the output path; otherwise
// LLVM style.
//
// Invocation:
//     apiary --module <name> --output <path> <header>... [-- <args>]
//     apiary --module <name> --output <path> -p <build-dir> <header>...
//
// Without --output, the formatted source is written to stdout. Without
// --module, "einsums" is used.

#include <string>
#include <unordered_set>

#include "DocsJson.hpp"
#include "Emitter.hpp"
#include "IR.hpp"
#include "MacroScanner.hpp"
#include "Properties.hpp"
#include "PyiEmitter.hpp"
#include "PythonOverloads.hpp"
#include "Visitor.hpp"
#include "clang/AST/ASTConsumer.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"

using namespace clang;
using namespace clang::tooling;

namespace {

// CommonOptionsParser owns -p and the trailing-arg conventions; we add
// tool-specific flags here. Static init is the libtooling pattern.
// NOLINTBEGIN(cert-err58-cpp,bugprone-throwing-static-initialization)
llvm::cl::OptionCategory g_tool_category("apiary options");

llvm::cl::opt<std::string> g_module_name("module", llvm::cl::desc("Python module name (PYBIND11_MODULE arg)"),
                                         llvm::cl::cat(g_tool_category), llvm::cl::init("einsums"));

llvm::cl::opt<std::string> g_output_path("output", llvm::cl::desc("Generated .cpp output path (default: stdout)"),
                                         llvm::cl::cat(g_tool_category), llvm::cl::init(""));

llvm::cl::opt<std::string> g_stub_output("stub-output",
                                         llvm::cl::desc("Path to write a Python type-stub (.pyi) file. "
                                                        "When set the .cpp emitter still runs (write to --output as usual); "
                                                        "the stub is written here. Without --stub-output no stub is produced."),
                                         llvm::cl::cat(g_tool_category), llvm::cl::init(""));

llvm::cl::opt<bool> g_dump_ir("dump-ir", llvm::cl::desc("Dump the parsed IR instead of emitting pybind11 (Phase 2 mode)"),
                              llvm::cl::cat(g_tool_category), llvm::cl::init(false));

llvm::cl::opt<bool> g_allow_empty("allow-empty",
                                  llvm::cl::desc("Emit a binding TU even when no declarations were bound. Without this "
                                                 "an empty result is an error: a module that binds nothing still "
                                                 "compiles, links, and imports, so it would otherwise pass unnoticed."),
                                  llvm::cl::cat(g_tool_category), llvm::cl::init(false));

llvm::cl::opt<bool> g_emit_cpp_docs_json("emit-cpp-docs-json",
                                         llvm::cl::desc("Emit a documentation JSON of the full PUBLIC C++ API of the "
                                                        "module headers (Option 2 — replaces Doxygen/Breathe). Walks all "
                                                        "documented public declarations, not just annotated ones. Same "
                                                        "JSON shape as --emit-docs-json; the C++ renderer uses the C++ "
                                                        "types (return_type/params) and emits cpp-domain directives."),
                                         llvm::cl::cat(g_tool_category), llvm::cl::init(false));

llvm::cl::opt<bool> g_report_undocumented("report-undocumented",
                                          llvm::cl::desc("With --emit-cpp-docs-json, print to stderr every public, "
                                                         "in-module-header entity (free function, class, enum, concept) "
                                                         "that is missing a doc comment, as "
                                                         "'file:line:col: undocumented <kind> <name>'. Produces a "
                                                         "punch-list of C++ API lacking Doxygen blocks; does not change "
                                                         "what is emitted. Pipe through 'sort -u' to dedupe across "
                                                         "per-module runs."),
                                          llvm::cl::cat(g_tool_category), llvm::cl::init(false));

llvm::cl::opt<bool> g_emit_docs_json("emit-docs-json",
                                     llvm::cl::desc("Emit a documentation-oriented JSON description of the Python-facing "
                                                    "surface instead of pybind11. Consumed by the docs generator; see "
                                                    "DocsJson.hpp for the schema. Honors --module for the recorded import "
                                                    "name and --output for the destination (default: stdout)."),
                                     llvm::cl::cat(g_tool_category), llvm::cl::init(false));

llvm::cl::list<std::string> g_source_includes("source-include",
                                              llvm::cl::desc("Header path to emit as `#include \"...\"` in the generated TU. "
                                                             "Repeat for multiple headers. Required so generated bindings can "
                                                             "name the C++ types they bind."),
                                              llvm::cl::cat(g_tool_category));

llvm::cl::opt<apiary::Target>
    g_target("target", llvm::cl::desc("Binding library to emit code against"),
             llvm::cl::values(clEnumValN(apiary::Target::Pybind11, "pybind11", "pybind11 (default)"),
                              clEnumValN(apiary::Target::Nanobind, "nanobind", "nanobind")),
             llvm::cl::cat(g_tool_category), llvm::cl::init(apiary::Target::Pybind11));

llvm::cl::opt<int> g_max_defs_per_tu("max-defs-per-tu",
                                     llvm::cl::desc("When > 0, split the generated binding TU into several smaller "
                                                    "TUs, each holding at most ~N binding statements (.def/.value/...). "
                                                    "Shards are written next to --output as '<stem>.shard<k><ext>' and a "
                                                    "dispatcher in shard 0 calls them in order, so consumers still call "
                                                    "one register symbol. Keeps heavily-instantiated modules from "
                                                    "exhausting compiler memory. 0 (default) emits a single TU."),
                                     llvm::cl::cat(g_tool_category), llvm::cl::init(0));

llvm::cl::opt<bool> g_report_defs("report-defs",
                                  llvm::cl::desc("Print to stdout the number of binding statements (.def/.value/...) "
                                                 "this module would emit, the emit-unit count, and the largest single "
                                                 "unit, then exit without emitting. Use it to pick a --max-defs-per-tu "
                                                 "budget: a shard can never be smaller than the largest unit."),
                                  llvm::cl::cat(g_tool_category), llvm::cl::init(false));

llvm::cl::opt<bool> g_plan("plan",
                           llvm::cl::desc("Print (to stdout, one per line) the shard output paths --max-defs-per-tu "
                                          "would produce for these headers, then exit without emitting. Lets a build "
                                          "system learn the generated filenames at configure time. Requires "
                                          "--max-defs-per-tu and --output."),
                           llvm::cl::cat(g_tool_category), llvm::cl::init(false));

llvm::cl::opt<std::string> g_register_fn("register-function",
                                         llvm::cl::desc("Emit a free function with this name that takes "
                                                        "(py::module_ &m) and registers all bindings, instead of "
                                                        "emitting a PYBIND11_MODULE block. Used by the "
                                                        "einsums_add_module autogen path so per-module TUs can be "
                                                        "aggregated under a single ``import einsums``."),
                                         llvm::cl::cat(g_tool_category), llvm::cl::init(""));
// NOLINTEND(cert-err58-cpp,bugprone-throwing-static-initialization)

// Per-translation-unit IR built up across all source files in a single
// run. Headers that get included by multiple of the module's other
// headers (e.g. Tensor.hpp pulled in via ArithmeticTensor.hpp +
// TensorForward.hpp + ...) cause the same annotated declaration to be
// visited once per TU. Dedupe by qualified_name so the emitter sees
// each declaration once.
apiary::Module         g_module;
std::unordered_set<std::string> g_seen_classes;
std::unordered_set<std::string> g_seen_functions;
std::unordered_set<std::string> g_seen_enums;
std::unordered_set<std::string> g_seen_typedefs;
std::unordered_set<std::string> g_seen_concepts;
std::unordered_set<std::string> g_seen_macros;
int                             g_error_count        = 0;
int                             g_undocumented_count = 0;
int                             g_annotated_seen         = 0;
int                             g_annotated_filtered_out = 0;

class IrConsumer : public ASTConsumer {
  public:
    void HandleTranslationUnit(ASTContext &ctx) override {
        apiary::Visitor visitor(ctx);
        // Constrain the visitor to only emit bindings for declarations that
        // live in this module's own headers. Without this, transitively
        // included headers from other modules (e.g. RuntimeTensor.hpp
        // pulled in via Operations.hpp) get re-bound here, causing
        // "an object with that name is already defined" at import time.
        std::vector<std::string> filter;
        for (std::string const &p : g_source_includes) {
            filter.push_back(p);
        }
        visitor.set_module_header_filter(filter);
        visitor.set_docs_mode(g_emit_cpp_docs_json);
        visitor.set_report_undocumented(g_report_undocumented);
        visitor.TraverseDecl(ctx.getTranslationUnitDecl());
        apiary::Module local = std::move(visitor).take();
        g_error_count += visitor.error_count();
        g_undocumented_count += visitor.undocumented_count();
        g_annotated_seen += visitor.annotated_seen();
        g_annotated_filtered_out += visitor.annotated_filtered_out();
        for (auto &c : local.classes) {
            if (g_seen_classes.insert(c.qualified_name).second) {
                g_module.classes.push_back(std::move(c));
            }
        }
        for (auto &f : local.functions) {
            // Key on qualified name PLUS parameter signature so that
            // overloaded free functions sharing a name (e.g. one
            // templated, one not — common for Python-bindable wrappers
            // around C++ overload sets) all survive the cross-TU dedupe.
            std::string key = f.qualified_name;
            key += '(';
            for (std::size_t i = 0; i < f.params.size(); ++i) {
                if (i != 0) {
                    key += ',';
                }
                key += f.params[i].type;
            }
            key += ')';
            if (g_seen_functions.insert(std::move(key)).second) {
                g_module.functions.push_back(std::move(f));
            }
        }
        for (auto &e : local.enums) {
            if (g_seen_enums.insert(e.qualified_name).second) {
                g_module.enums.push_back(std::move(e));
            }
        }
        // Docs-mode entities (typedefs/concepts). Dedupe by qualified name
        // across TUs the same way as the others.
        for (auto &t : local.typedefs) {
            if (g_seen_typedefs.insert(t.qualified_name).second) {
                g_module.typedefs.push_back(std::move(t));
            }
        }
        for (auto &c : local.concepts) {
            if (g_seen_concepts.insert(c.qualified_name).second) {
                g_module.concepts.push_back(std::move(c));
            }
        }
        // Documented macros are not AST decls — scan the main header's raw
        // source text (docs mode only). Reading raw text covers all #if
        // branches, so a macro documented inside a compiler-specific branch
        // is still captured.
        if (g_emit_cpp_docs_json) {
            clang::SourceManager const &sm  = ctx.getSourceManager();
            llvm::StringRef const       buf = sm.getBufferData(sm.getMainFileID());
            for (auto &m : apiary::scan_macros(buf)) {
                if (g_seen_macros.insert(m.qualified_name).second) {
                    g_module.macros.push_back(std::move(m));
                }
            }
        }
    }
};

class IrAction : public ASTFrontendAction {
  protected:
    std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance & /*ci*/, llvm::StringRef /*file*/) override {
        return std::make_unique<IrConsumer>();
    }
};

int write_output(std::string const &content) {
    if (g_output_path.empty()) {
        llvm::outs() << content;
        return 0;
    }
    std::error_code      ec;
    llvm::raw_fd_ostream out(g_output_path, ec);
    if (ec) {
        llvm::errs() << "apiary: cannot open output '" << g_output_path << "': " << ec.message() << "\n";
        return 1;
    }
    out << content;
    return 0;
}

} // namespace

int main(int argc, char const **argv) {
    auto expected = CommonOptionsParser::create(argc, argv, g_tool_category);
    if (!expected) {
        llvm::errs() << llvm::toString(expected.takeError());
        return 1;
    }
    CommonOptionsParser &options = *expected;
    ClangTool            tool(options.getCompilations(), options.getSourcePathList());
    int const            rc = tool.run(newFrontendActionFactory<IrAction>().get());

    // Resolve the dispatcher-grouping decisions for every templated free
    // function. Both the C++ emitter and the (future) .pyi emitter
    // consume the precomputed view via BoundFunction.python_overloads.
    apiary::compute_python_overloads(g_module);

    // Collapse @getter/@setter pairs into BoundClass.properties so
    // the .pyi emitter can render Python @property entries directly.
    apiary::compute_properties(g_module);

    if (g_dump_ir) {
        return write_output(apiary::dump(g_module));
    }

    if (g_emit_docs_json || g_emit_cpp_docs_json) {
        return write_output(apiary::emit_docs_json(g_module, g_module_name));
    }

    apiary::EmitOptions opts;
    opts.module_name            = g_module_name;
    opts.register_function_name = g_register_fn;
    opts.source_path_for_format = g_output_path.empty() ? std::string("generated.cpp") : g_output_path;
    opts.target                 = g_target;
    for (std::string const &p : g_source_includes) {
        opts.source_includes.push_back(p);
    }

    // --report-defs: print the module's binding-statement tally and exit, so a
    // budget for --max-defs-per-tu can be sized empirically.
    if (g_report_defs) {
        apiary::DefReport const rep = apiary::report_defs(g_module, opts);
        llvm::outs() << "apiary: module '" << g_module_name << "': " << rep.total_defs << " binding statement(s) across "
                     << rep.unit_count << " emit unit(s); largest single unit = " << rep.max_unit_defs
                     << " (a shard can never be smaller than this).\n";
        return g_error_count > 0 ? 1 : (rc != 0 ? rc : 0);
    }

    // Measured on the *emitted* binding statements, not the IR entity counts.
    // Those differ: an entity can survive into the IR and still contribute no
    // binding (an exposed class whose members were all filtered out), so the IR
    // counts would call such a module non-empty when the generated TU binds
    // nothing at all.
    apiary::DefReport const report = apiary::report_defs(g_module, opts);

    // A tally on every run, not just on failure. An empty module is otherwise
    // indistinguishable from a healthy one in a build log, and this line is
    // what makes the difference visible.
    llvm::errs() << "apiary: module '" << g_module_name << "': " << report.total_defs << " binding statement(s) from "
                 << g_module.classes.size() << " class(es), " << g_module.functions.size() << " function(s), "
                 << g_module.enums.size() << " enum(s)\n";

    // Binding nothing is an error by default. Such a module still compiles,
    // links, and imports, so every downstream check passes while it exposes
    // nothing - the failure is invisible exactly where it matters. The counters
    // separate the two causes, which need opposite fixes.
    if (report.total_defs == 0 && !g_allow_empty) {
        llvm::errs() << "apiary: no bindings were generated.\n";
        if (g_annotated_seen == 0) {
            llvm::errs() << "apiary:   no APIARY_* annotations were found. Check that the header includes "
                            "<apiary/Annotations.hpp> and that declarations carry APIARY_EXPOSE.\n";
        } else if (g_annotated_filtered_out > 0) {
            llvm::errs() << "apiary:   " << g_annotated_filtered_out
                         << " annotated declaration(s) were rejected by the --source-include filter, so the paths "
                            "below do not match where those declarations actually live:\n";
            for (std::string const &p : g_source_includes) {
                llvm::errs() << "apiary:     --source-include " << p << "\n";
            }
        }
        llvm::errs() << "apiary: refusing to write an empty module; pass --allow-empty if this is intentional.\n";
        // Deliberately writes nothing. An empty-but-valid TU on disk is worse
        // than no file at all: the next build step consumes it happily.
        return 1;
    }

    // Sharding: when --max-defs-per-tu is set, the binding body is split
    // across several smaller TUs (named off --output) so a heavily
    // instantiated module doesn't produce one TU large enough to exhaust the
    // compiler's memory. --plan just reports the shard filenames (for
    // configure-time wiring) and exits.
    if (g_max_defs_per_tu > 0 || g_plan) {
        if (g_output_path.empty()) {
            llvm::errs() << "apiary: --max-defs-per-tu/--plan require --output (shard filenames are derived from it).\n";
            return 1;
        }
        if (g_plan && g_max_defs_per_tu <= 0) {
            llvm::errs() << "apiary: --plan requires --max-defs-per-tu > 0.\n";
            return 1;
        }
        if (g_plan) {
            for (std::string const &p : apiary::plan_shards(g_module, opts, g_max_defs_per_tu, g_output_path)) {
                llvm::outs() << p << "\n";
            }
            return g_error_count > 0 ? 1 : (rc != 0 ? rc : 0);
        }
        for (apiary::ShardFile const &shard : apiary::emit_shards(g_module, opts, g_max_defs_per_tu, g_output_path)) {
            std::error_code      ec;
            llvm::raw_fd_ostream out(shard.path, ec);
            if (ec) {
                llvm::errs() << "apiary: cannot open shard output '" << shard.path << "': " << ec.message() << "\n";
                return 1;
            }
            out << shard.content;
        }
    } else {
        std::string const generated = apiary::emit(g_module, opts);
        if (int const single_rc = write_output(generated); single_rc != 0) {
            return single_rc;
        }
    }

    // Optionally also emit a Python stub (.pyi) file for pyright. The
    // stub mirrors what the C++ binding TU exposes — same py_names,
    // same submodule routing — so adding the flag to a build just adds
    // a sibling .pyi alongside the .cpp.
    if (!g_stub_output.empty()) {
        apiary::PyiOptions stub_opts;
        stub_opts.banner          = "module: " + std::string{g_module_name};
        std::string const    stub = apiary::emit_pyi(g_module, stub_opts);
        std::error_code      ec;
        llvm::raw_fd_ostream out(g_stub_output, ec);
        if (ec) {
            llvm::errs() << "apiary: cannot open stub output '" << g_stub_output << "': " << ec.message() << "\n";
            return 1;
        }
        out << stub;
    }

    if (g_report_undocumented) {
        llvm::errs() << "apiary: " << g_undocumented_count << " undocumented public entit"
                     << (g_undocumented_count == 1 ? "y" : "ies") << ".\n";
    }

    if (g_error_count > 0) {
        llvm::errs() << "apiary: " << g_error_count << " error(s) — bindings may be incomplete.\n";
        return 1;
    }
    return rc;
}
