//----------------------------------------------------------------------------------------------
// Copyright (c) The Einsums Developers. All rights reserved.
// Licensed under the MIT License. See LICENSE.txt in the project root for license information.
//----------------------------------------------------------------------------------------------

#pragma once

#include <string>

#include "clang/AST/ASTContext.h"
#include "clang/AST/Type.h"

namespace apiary {

/// @brief Returns a pretty-printed C++ form of `type` suitable for emission in pybind11 binding code.
/// @param type The Clang type to translate.
/// @param ctx The AST context owning `type`.
/// @return A pretty-printed C++ form of the type.
/// @note Phase-2 stub: currently relies on Clang's own PrintingPolicy; Phase 3 will extend this to
///       handle holder rewrites (e.g. unique_ptr<T> -> shared_ptr<T> when an APIARY_HOLDER directive
///       is in effect) and pybind11-specific type substitutions.
std::string translate_type(clang::QualType type, clang::ASTContext const &ctx);

/// @brief Best-effort Python-stub form of `type`, suitable for emission in a `.pyi` file consumed by pyright.
/// @param type The Clang type to translate.
/// @param ctx The AST context owning `type`.
/// @return The Python-stub form of the type. Maps fundamentals to Python builtins, std
///         containers/optional/pair/tuple/variant/function to their typing equivalents, and strips
///         cv/ref/ptr qualifiers.
/// @note Anything unknown (most notably bound class types) is returned as the canonical qualified C++
///       name; a post-pass over the IR resolves those against bound classes.
std::string translate_python_type(clang::QualType type, clang::ASTContext const &ctx);

/// @brief Best-effort translation of a captured default-argument expression to its Python-literal form.
/// @param cpp_default The captured C++ default-argument expression text.
/// @return The Python-literal form of the default. Strips integer/float suffixes, rewrites `nullptr` /
///         `std::nullopt` to `None`, `true`/`false` to `True`/`False`, and falls back to the verbatim
///         text when no rewrite applies.
std::string translate_python_default(std::string const &cpp_default);

/// @brief String-based variant of `translate_python_type` for callers that already have a printed C++ type name.
/// @param cpp_type The printed C++ type name (no `clang::QualType` access required).
/// @return The Python-stub form of the type. Same recursion rules: maps fundamentals, `std::vector<T>` →
///         `list[T_py]`, `std::pair<A,B>` → `tuple[A_py, B_py]`, etc.
std::string translate_python_type_string(std::string const &cpp_type);

} // namespace apiary
