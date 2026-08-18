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
#include <string_view>
#include <unordered_set>

#include "DocsJson.hpp"
#include "Diagnostics.hpp"
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
#include "llvm/Support/MemoryBuffer.h"
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

llvm::cl::opt<int> g_num_tu("num-tu",
                            llvm::cl::desc("When > 1, split the generated binding TU into exactly N smaller TUs, "
                                           "balanced by binding-statement count. Same output shape as "
                                           "--max-defs-per-tu ('<stem>.shard<k><ext>' plus a dispatcher in shard 0), "
                                           "but the file names follow from N alone, so a build system can declare "
                                           "them without running apiary first. Shards past the last unit are emitted "
                                           "empty to keep the count exact. Mutually exclusive with "
                                           "--max-defs-per-tu. 0 (default) emits a single TU."),
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

llvm::cl::list<std::string> g_diagnostic("diagnostic",
                                        llvm::cl::desc("Set one diagnostic check's severity as <check>=<severity>, where "
                                                       "severity is ignored, note, warning or error. Repeatable. Use "
                                                       "--list-diagnostics to see the check ids and their defaults."),
                                        llvm::cl::cat(g_tool_category), llvm::cl::value_desc("check=severity"));

llvm::cl::opt<bool> g_werror("werror", llvm::cl::desc("Promote every warning-severity diagnostic to an error."),
                             llvm::cl::cat(g_tool_category), llvm::cl::init(false));

llvm::cl::opt<bool> g_list_diagnostics("list-diagnostics",
                                       llvm::cl::desc("Print the diagnostic check ids, their default severities and what "
                                                      "each means, then exit."),
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
std::unordered_set<std::string> g_seen_variables;
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
        for (auto &v : local.variables) {
            if (g_seen_variables.insert(v.qualified_name).second) {
                g_module.variables.push_back(std::move(v));
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

/// True when @p path already holds exactly @p content.
///
/// Generation is deterministic, so re-running on unchanged headers produces a
/// byte-identical file. Writing it anyway only updates the mtime - which is
/// what every build system downstream actually reads, so an unconditional
/// write turns "nothing changed" into a recompile of the generated TU and a
/// relink of everything it feeds. Comparing first costs one read of a file we
/// were about to overwrite.
bool file_has_content(std::string const &path, std::string const &content) {
    auto buf = llvm::MemoryBuffer::getFile(path, /*IsText=*/false, /*RequiresNullTerminator=*/false);
    return buf && (*buf)->getBuffer() == content;
}

int write_file(std::string const &path, std::string const &content) {
    if (file_has_content(path, content)) {
        return 0;
    }
    std::error_code      ec;
    llvm::raw_fd_ostream out(path, ec);
    if (ec) {
        llvm::errs() << "apiary: cannot open output '" << path << "': " << ec.message() << "\n";
        return 1;
    }
    out << content;
    return 0;
}

int write_output(std::string const &content) {
    if (g_output_path.empty()) {
        llvm::outs() << content;
        return 0;
    }
    return write_file(g_output_path, content);
}

} // namespace

int main(int argc, char const **argv) {
    // Answered before CommonOptionsParser, which requires at least one source
    // path: asking what the diagnostic checks ARE should not require naming a
    // header to run them against.
    for (int i = 1; i < argc; ++i) {
        if (std::string_view{argv[i]} == "--list-diagnostics") {
            apiary::diag::describe_checks();
            return 0;
        }
    }

    auto expected = CommonOptionsParser::create(argc, argv, g_tool_category);
    if (!expected) {
        llvm::errs() << llvm::toString(expected.takeError());
        return 1;
    }

    // Diagnostic policy is settled before any work starts: a --diagnostic typo
    // must not be discovered after the tool has already emitted a file under a
    // severity the caller did not ask for.
    for (std::string const &spec : g_diagnostic) {
        std::string err;
        if (!apiary::diag::set_severity_from_spec(spec, err)) {
            llvm::errs() << "apiary: --diagnostic: " << err << "\n";
            return 2;
        }
    }
    if (g_werror) {
        apiary::diag::promote_warnings_to_errors();
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

    // Remove anything whose Python name is not a Python identifier, reporting
    // each, so both emitters see the same module and neither can bind a name
    // the other cannot spell.
    //
    // AFTER the docs and dump-ir exits on purpose. Bindability and
    // documentability are different contracts: a free ``operator+`` is
    // perfectly documentable - the C++ reference has a page for exactly that -
    // and dropping it there would delete real API documentation to solve a
    // Python-naming problem it does not have.
    apiary::drop_unbindable_names(g_module);

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
        apiary::diag::print_summary();
        return (g_error_count + apiary::diag::errors()) > 0 ? 1 : (rc != 0 ? rc : 0);
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
    //
    // Led by the artifact rather than by --module: a project that aggregates
    // many C++ modules into one Python module passes the same --module to
    // every run, so a build log carries a dozen of these lines all naming
    // 'einsums' and nothing tying a count back to the run that produced it.
    // The build system's own progress line is adjacent only until the build
    // goes parallel.
    std::string source_label{g_output_path};
    if (std::size_t const slash = source_label.find_last_of("/\\"); slash != std::string::npos) {
        source_label.erase(0, slash + 1);
    }
    if (source_label.empty()) {
        // Writing to stdout. The register function is the next most specific
        // name the run carries; standalone form has only the module name.
        source_label = g_register_fn.empty() ? std::string{g_module_name} : std::string{g_register_fn};
    }

    llvm::errs() << "apiary: " << source_label << ": " << report.total_defs << " binding statement(s) from "
                 << g_module.classes.size() << " class(es), " << g_module.functions.size() << " function(s), "
                 << g_module.enums.size() << " enum(s) in module '" << g_module_name << "'\n";

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

    // Everything the run would write, generated but not yet on disk. See the
    // decision point below for why.
    struct PendingWrite {
        std::string path; // empty -> stdout
        std::string content;
    };
    std::vector<PendingWrite> pending;

    // Sharding: when --max-defs-per-tu or --num-tu is set, the binding body
    // is split across several smaller TUs (named off --output) so a heavily
    // instantiated module doesn't produce one TU large enough to exhaust the
    // compiler's memory. The two differ in who picks the shard count, and so
    // in whether the build system can name the outputs on its own: under
    // --num-tu it can, under --max-defs-per-tu it needs --plan, which reports
    // the shard filenames and exits.
    apiary::ShardSpec const shard_spec{g_max_defs_per_tu, g_num_tu};
    if (shard_spec.sharded() || g_plan) {
        if (g_max_defs_per_tu > 0 && g_num_tu > 0) {
            llvm::errs() << "apiary: --max-defs-per-tu and --num-tu are mutually exclusive - one sizes the shards and "
                            "lets the count follow, the other fixes the count.\n";
            return 1;
        }
        if (g_output_path.empty()) {
            llvm::errs() << "apiary: --max-defs-per-tu/--num-tu/--plan require --output (shard filenames are derived from it).\n";
            return 1;
        }
        if (g_plan && !shard_spec.sharded()) {
            llvm::errs() << "apiary: --plan requires --max-defs-per-tu > 0 or --num-tu > 1.\n";
            return 1;
        }
        if (g_plan) {
            for (std::string const &p : apiary::plan_shards(g_module, opts, shard_spec, g_output_path)) {
                llvm::outs() << p << "\n";
            }
            apiary::diag::print_summary();
            return (g_error_count + apiary::diag::errors()) > 0 ? 1 : (rc != 0 ? rc : 0);
        }
        for (apiary::ShardFile const &shard : apiary::emit_shards(g_module, opts, shard_spec, g_output_path)) {
            pending.push_back({shard.path, shard.content});
        }
    } else {
        pending.push_back({g_output_path, apiary::emit(g_module, opts)});
    }

    // Optionally also emit a Python stub (.pyi) file for pyright. The
    // stub mirrors what the C++ binding TU exposes — same py_names,
    // same submodule routing — so adding the flag to a build just adds
    // a sibling .pyi alongside the .cpp.
    if (!g_stub_output.empty()) {
        apiary::PyiOptions stub_opts;
        stub_opts.banner = "module: " + std::string{g_module_name};
        pending.push_back({g_stub_output, apiary::emit_pyi(g_module, stub_opts)});
    }

    if (g_report_undocumented) {
        llvm::errs() << "apiary: " << g_undocumented_count << " undocumented public entit"
                     << (g_undocumented_count == 1 ? "y" : "ies") << ".\n";
    }

    // Decide BEFORE writing. Emission is where the emitters report what they
    // could not represent, so the exit status is not knowable until every
    // artifact has been generated - but it IS knowable before any of them
    // reaches disk. Writing first left a failing run's output on disk looking
    // fresh: a build system that reruns failed edges recovers, a manual
    // invocation or a tolerant driver does not. This is the same instinct as
    // the empty-module refusal above, which already declines to write rather
    // than leave an empty-but-valid TU for the next step to consume happily.
    apiary::diag::print_summary();
    g_error_count += apiary::diag::errors();
    if (g_error_count > 0) {
        llvm::errs() << "apiary: " << g_error_count << " error(s); refusing to write "
                     << (pending.size() == 1 ? "the generated file" : "the generated files") << ".\n";
        return 1;
    }

    for (PendingWrite const &w : pending) {
        // An empty path means the single-output case with no --output: stdout.
        int const write_rc = w.path.empty() ? write_output(w.content) : write_file(w.path, w.content);
        if (write_rc != 0) {
            return write_rc;
        }
    }
    return rc;
}
