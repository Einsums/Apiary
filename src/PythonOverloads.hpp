//----------------------------------------------------------------------------------------------
// Copyright (c) The Einsums Developers. All rights reserved.
// Licensed under the MIT License. See LICENSE.txt in the project root for license information.
//----------------------------------------------------------------------------------------------

#pragma once

// Post-IR pass that decides how each templated free function's raw
// instantiation list collapses into Python-facing entries. Mirrors the
// grouping logic the pybind11 emitter expects:
//
//   * @instantiate_as overloads sharing a Python name AND argument
//     signature (only the return type varies) collapse into one
//     dispatcher taking a ``dtype="..."`` kwarg.
//   * 2^N instantiations from APIARY_INSTANTIATE_BOOLS collapse
//     into one dispatcher with N bool kwargs (kw_only).
//   * Otherwise each instantiation is its own pybind11 overload (or the
//     sole entry, in the non-template case).
//
// Both the pybind11 C++ emitter and the .pyi stub emitter consume the
// resulting `BoundFunction.python_overloads` view so the merge rules
// don't need to be reimplemented across the two backends.

#include <string>
#include <vector>

#include "IR.hpp"

namespace apiary {

/// @brief Compute and assign `f.python_overloads`.
/// @param f The bound function whose `python_overloads` are computed and assigned.
/// @note Idempotent — calling twice on the same function clears the previous result first.
/// @warning Calls to this must happen after the Visitor has finished populating instantiations.
void compute_python_overloads(BoundFunction &f);

/// @brief Walk every function in `module_` and run compute_python_overloads on it.
/// @param module_ The module whose functions are processed.
void compute_python_overloads(Module &module_);

// ── Helpers exposed for reuse by the emitter ──────────────────────────

/// @brief Split a per-instantiation comma-joined string into individual values, respecting `<>` nesting.
/// @param combo The comma-joined instantiation string, e.g. ``"float, 2"``.
/// @return The individual values, e.g. ``{"float", "2"}``.
std::vector<std::string> split_instantiation_args(std::string const &combo);

/// @brief Drop every entity whose Python name is not a Python identifier.
///
/// A C++ name is not always a Python one. A free ``operator*`` exposed without
/// a rename reached both emitters verbatim: the binding registered it under a
/// name no Python caller can type, and the stub emitted ``def operator*(...)``,
/// which is a syntax error - so the whole ``.pyi`` failed to parse and the type
/// checker got nothing from the file, not just nothing for that function.
///
/// apiary cannot represent this, so it says so and binds nothing rather than
/// emitting something unusable. The report is an error, not a warning: the
/// author wrote APIARY_EXPOSE, the fix is one annotation away
/// (``APIARY_RENAME``, or ``APIARY_OPERATOR`` on a class member), and a build
/// that quietly lacks a function the source asked for is worse than one that
/// stops.
///
/// Filtering the module once, before either emitter runs, rather than checking
/// at each of the fifteen places a name is chosen: one policy, one diagnostic,
/// and no way for the two emitters to disagree about what got bound.
///
/// @param module_ The module to filter in place.
void drop_unbindable_names(Module &module_);

/// @brief One C++ parameter after a template parameter pack has been expanded.
struct ExpandedParam {
    /// Concrete parameter type, with the pack's template parameter substituted.
    std::string type;
    /// Parameter name; an expanded pack element gets its index appended.
    std::string name;
};

/// @brief Expand a trailing parameter pack against an instantiation's arguments.
///
/// A pack has no arity until it is instantiated, and `Ts... values` reaches
/// both emitters as the single parameter ``Ts...``. Re-emitted verbatim that is
/// a disaster in either target: ``double...`` in a C++ cast is VARARGS, so the
/// cast names nothing and the TU will not compile, and in a `.pyi` it is not
/// even valid syntax, which costs the type checker the whole file.
///
/// The instantiation supplies the arity. Template parameters before the pack
/// consume one argument each; the pack takes the rest - which is the only
/// arrangement a function template's pack can have, since it must be last to be
/// deducible.
///
/// @param params Declared parameters, as (type, name) pairs.
/// @param template_param_names The function template's parameter names, in order.
/// @param type_args The instantiation's type arguments, already split.
/// @return The parameter list with any pack expanded; unchanged when there is none.
std::vector<ExpandedParam> expand_parameter_pack(std::vector<ExpandedParam> const &params,
                                                 std::vector<std::string> const   &template_param_names,
                                                 std::vector<std::string> const   &type_args);

/// @brief Map a C++ scalar type to its accepted dtype-string aliases.
/// @param cpp_type The C++ scalar type to map.
/// @return The accepted dtype-string aliases, or empty when the type isn't a recognized dtype.
/// @note The dispatcher path only triggers for known dtypes.
std::vector<std::string> dtype_aliases_for(std::string const &cpp_type);

/// @brief Pick the default dtype string for a dispatcher group.
/// @param dtype_values_in_order The dtype values in instantiation order.
/// @return The default dtype string.
/// @note Numpy convention favors ``float64`` (``double``) when present; otherwise the first instance's first alias.
std::string pick_default_dtype(std::vector<std::string> const &dtype_values_in_order);

} // namespace apiary
