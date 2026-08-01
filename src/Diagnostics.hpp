//----------------------------------------------------------------------------------------------
// Copyright (c) The Einsums Developers. All rights reserved.
// Licensed under the MIT License. See LICENSE.txt in the project root for license information.
//----------------------------------------------------------------------------------------------

#pragma once

// Diagnostics - the channel by which the second half of the pipeline can say
// "I could not represent this".
//
// The Visitor could always report: it walks the AST, holds an ASTContext, and
// counts its own errors into main's exit status. Everything downstream of it
// could not. Emitter, PyiEmitter, TypeTranslator and DocsJson decide what C++,
// what stubs and what reST come out, and between them they had zero
// diagnostics - every `throw` in Emitter.cpp is a string in the GENERATED code,
// not apiary reporting a problem. So when an input could not be represented,
// the tool emitted something wrong and exited 0.
//
// That is not hypothetical. A ref-qualified overload produced a static_cast
// naming no overload (the generated TU does not compile); a parameter pack
// produced `double...`, which is C varargs in the cast and invalid syntax in
// the stub; a C++ name that is not a Python identifier reached the stub
// verbatim and cost the type checker the whole file. Each was found by
// compiling or parsing the output, because the tool itself said nothing.
//
// Design notes:
//
//   - Checks have STABLE IDS and per-id severity, the same shape
//     apiary_doc_lint.py uses. A project can ratchet: start at warning, fix,
//     then pin to error so it stays fixed.
//   - Locations come from the IR's SourceLocation, not from clang. The
//     emitters run over the Module long after the AST is gone, and a
//     diagnostic that cannot name the header it came from is the thing this
//     module exists to stop.
//   - The sink is process-global rather than threaded through every emitter
//     signature. apiary is one-shot: a single TU set, a single run, a single
//     exit status, and main already keeps its counters this way. Threading a
//     reference through the ~4000 lines of the two emitters would be a large
//     mechanical change whose only benefit is a testability the tool's shape
//     does not need.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "IR.hpp"

namespace apiary::diag {

/// @brief How loudly a check speaks, and whether it fails the run.
enum class Severity : std::uint8_t {
    Ignored, ///< Not reported at all.
    Note,    ///< Reported; does not affect the exit status.
    Warning, ///< Reported; fails the run only under --werror.
    Error,   ///< Reported; fails the run.
};

/// @brief Parse a severity name (``ignored`` / ``note`` / ``warning`` / ``error``).
/// @param name The severity name, case-sensitive.
/// @param out Set to the parsed severity on success.
/// @return `true` when `name` was recognized.
bool parse_severity(std::string_view name, Severity &out);

/// @brief Every check id the tool knows, in registration order.
/// @return The known check ids.
std::vector<std::string_view> known_checks();

/// @brief Print every check id, its CURRENT severity and what it means.
///
/// Current rather than default, so the listing also answers "what did my
/// --diagnostic flags actually do".
void describe_checks();

/// @brief Override one check's severity.
/// @param check The check id.
/// @param severity The new severity.
/// @return `false` when `check` is not a known id (the caller should complain).
bool set_severity(std::string_view check, Severity severity);

/// @brief Apply a ``check=severity`` spec, as taken from ``--diagnostic``.
/// @param spec The spec text.
/// @param error Set to a human-readable reason when parsing fails.
/// @return `true` on success.
bool set_severity_from_spec(std::string_view spec, std::string &error);

/// @brief Promote every Warning-severity check to Error (``--werror``).
void promote_warnings_to_errors();

/// @brief Report one diagnostic against a source location.
///
/// Formatted as ``file:line:col: severity: apiary: message [check]`` on
/// stderr, matching both the Visitor's existing output and what editors and
/// CI log scrapers expect.
///
/// @param check The check id; an unknown id is a programming error and is
///              reported as such rather than silently dropped.
/// @param loc Where the offending entity was declared.
/// @param message What could not be represented, and what that costs.
void report(std::string_view check, SourceLocation const &loc, std::string const &message);

/// @brief Number of Error-severity diagnostics reported so far.
/// @return The error count.
int errors();

/// @brief Number of Warning-severity diagnostics reported so far.
/// @return The warning count.
int warnings();

/// @brief Print the trailing summary line, when anything was reported.
void print_summary();

} // namespace apiary::diag
