//----------------------------------------------------------------------------------------------
// Copyright (c) The Einsums Developers. All rights reserved.
// Licensed under the MIT License. See LICENSE.txt in the project root for license information.
//----------------------------------------------------------------------------------------------

#pragma once

#include <string>
#include <vector>

namespace apiary {

/// @brief One keyword/values group parsed from an @instantiate directive payload.
///
/// @note The keyword is load-bearing — Visitor::collect_instantiations matches each
/// keyword against the class's actual template-parameter names and refuses to emit
/// bindings on mismatch, giving a clean diagnostic if a stray macro mangled the payload.
struct ParamGroup {
    /// @brief Template-parameter name this group binds (e.g. "T" or "Rank").
    std::string              keyword;
    /// @brief The set of values to instantiate for this keyword.
    std::vector<std::string> values;
};

/// @brief Parsed result of an @instantiate directive: a class name and its parameter groups.
struct InstantiateSpec {
    /// @brief The class being instantiated (e.g. "Tensor").
    std::string             class_name;
    /// @brief Ordered keyword/values groups making up the cross product.
    std::vector<ParamGroup> groups;
};

/// @brief Parses the payload of an @instantiate directive into an InstantiateSpec.
///
/// The raw text after the macro expands (and after AnnotationParser strips the prefix)
/// looks like `Tensor, T(float, double, std::complex<float>), Rank(1, 2, 3)`, which is
/// split into a class name and per-keyword value groups. The parser respects nested
/// `<>` and `()` so commas inside template arguments don't split the wrong list.
///
/// @param payload The raw directive payload text.
/// @return The parsed class name and parameter groups.
InstantiateSpec parse_instantiate(std::string const &payload);

/// @brief Parsed result of an @instantiate_as directive.
struct InstantiateAsSpec {
    /// @brief The Python-facing name (e.g. "Tensor2d").
    std::string py_name;
    /// @brief The leading class name extracted from the type expression.
    std::string class_name;
    /// @brief The angle-bracket payload emitted between `<` and `>`.
    std::string type_args;
};

/// @brief Parses an @instantiate_as directive into an InstantiateAsSpec.
///
/// The directive arrives with two well-defined args from AnnotationParser: the Python
/// name (e.g. "Tensor2d") and a full concrete C++ type expression (e.g. "Tensor<double, 2>").
/// The type expression is split into the angle-bracket payload and the leading class name.
///
/// @param py_name The Python-facing name for the instantiation.
/// @param type_expr The full concrete C++ type expression.
/// @return The parsed Python name, class name, and type arguments.
InstantiateAsSpec parse_instantiate_as(std::string const &py_name, std::string const &type_expr);

/// @brief Expands an ordered list of value lists into every cross-product combination.
///
/// Each combination is returned as a comma-joined string ready to paste between `<` and
/// `>`, e.g. ([float,double], [1,2]) -> ["float, 1", "float, 2", "double, 1", "double, 2"].
///
/// @param lists The ordered value lists to combine.
/// @return Every combination as a comma-joined string.
std::vector<std::string> cross_product(std::vector<std::vector<std::string>> const &lists);

/// @brief Builds a Python identifier from a class base name and a comma-joined argument string.
///
/// Non-identifier characters collapse to underscores, so `Tensor` + `std::complex<float>, 2`
/// becomes `Tensor_std_complex_float_2`.
///
/// @param base The class base name.
/// @param type_args The comma-joined argument string.
/// @return A valid Python identifier.
std::string sanitize_python_name(std::string const &base, std::string const &type_args);

} // namespace apiary
