//----------------------------------------------------------------------------------------------
// Copyright (c) The Einsums Developers. All rights reserved.
// Licensed under the MIT License. See LICENSE.txt in the project root for license information.
//----------------------------------------------------------------------------------------------

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "IR.hpp"

namespace apiary {

/// @brief Selects which Python-binding library the emitter writes against.
///
/// @note Pybind11 is the default, battle-tested target.
/// @note Nanobind is newer, ~3-5× faster compile/binary/runtime. Mostly
///   API-compatible with pybind11 but with renamed enums
///   (``nb::rv_policy`` vs ``py::return_value_policy``), a different
///   module macro (``NB_MODULE``), and a split STL header layout.
/// @warning Nanobind has no direct ``def_buffer`` analog — buffer protocol
///   directives are dropped under the nanobind target.
enum class Target : std::uint8_t { Pybind11, Nanobind };

/// @brief Configuration for a single emit pass.
///
/// The emitter has two output shapes:
/// @note Standalone module — emit ``PYBIND11_MODULE(<module_name>, m) { ... }``
///   (or ``NB_MODULE``) producing a self-contained ``.so`` importable
///   by name. Used by the standalone fixtures and goldens.
/// @note Register function — emit ``void <register_function_name>(<ns>::module_ &m)
///   { ... }`` which the aggregator main calls from a single
///   module-macro block. Used by the einsums_add_module autogen path
///   so every module ends up under one ``import einsums``.
struct EmitOptions {
    /// module-macro arg (standalone form)
    std::string              module_name;
    /// register-function symbol (aggregator form)
    std::string              register_function_name;
    /// path used to discover .clang-format
    std::string              source_path_for_format;
    /// headers the bindings refer to (emitted as #include "...")
    std::vector<std::string> source_includes;
    /// Python-binding library target (defaults to Pybind11).
    Target                   target = Target::Pybind11;
};

/// @brief Emit C++ binding code for `module_` and return it as a string.
///
/// Output is post-clang-format using the project's .clang-format if one is
/// found via `opts.source_path_for_format`; otherwise LLVM style is used.
///
/// @param module_ The IR module to emit bindings for.
/// @param opts Emit configuration (output shape, target, formatting path, includes).
/// @return The emitted, clang-formatted C++ binding source.
std::string emit(Module const &module_, EmitOptions const &opts);

/// @brief One generated shard: its output path and its formatted contents.
struct ShardFile {
    std::string path;
    std::string content;
};

/// @brief Emit `module_`'s bindings split across several smaller TUs.
///
/// The body is cut into contiguous, order-preserving shards so that no single
/// translation unit exceeds roughly `max_defs` binding statements. Shard 0
/// also carries a dispatcher (the register function, or the module macro in
/// standalone form) that calls every `<base>__shard<k>` in order, so the
/// ordering invariants of the single-TU path are preserved and consumers keep
/// calling one symbol.
///
/// When everything fits in a single shard the result is one file at
/// `base_output_path` whose contents are byte-identical to `emit()`.
///
/// @param module_ The IR module to emit bindings for.
/// @param opts Emit configuration (output shape, target, formatting path, includes).
/// @param max_defs Approximate per-TU budget in binding statements (> 0).
/// @param base_output_path The would-be single-TU path; shard names are
///   derived from it as `<stem>.shard<k><ext>`.
/// @return One entry per shard, each with its destination path and contents.
std::vector<ShardFile> emit_shards(Module const &module_, EmitOptions const &opts, int max_defs,
                                   std::string const &base_output_path);

/// @brief Compute the shard output paths without emitting their contents.
///
/// Runs the same partition as `emit_shards()` so a build system can learn the
/// generated filenames at configure time. Returns `{ base_output_path }` when
/// the module fits in a single TU.
std::vector<std::string> plan_shards(Module const &module_, EmitOptions const &opts, int max_defs,
                                     std::string const &base_output_path);

/// @brief Per-module tally of binding statements, for sizing
/// `--max-defs-per-tu` before committing a budget.
struct DefReport {
    /// All binding statements (.def / .value / register_exception) the module
    /// would emit — the quantity `--max-defs-per-tu` is measured against.
    int total_defs = 0;
    /// Number of indivisible emit units (one enum, one class *instantiation*,
    /// or one free function). Sharding splits between units, never within one.
    int unit_count = 0;
    /// Largest single unit's statement count. A shard can never be smaller
    /// than this, so a budget below it yields one oversized shard rather than
    /// a finer split.
    int max_unit_defs = 0;
};

/// @brief Tally binding statements for `module_` without emitting code.
///
/// Uses the same units and cost metric as `emit_shards()`, so the numbers are
/// directly comparable to a `--max-defs-per-tu` budget.
DefReport report_defs(Module const &module_, EmitOptions const &opts);

} // namespace apiary
