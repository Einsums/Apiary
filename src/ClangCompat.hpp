//----------------------------------------------------------------------------------------------
// Copyright (c) The Einsums Developers. All rights reserved.
// Licensed under the MIT License. See LICENSE.txt in the project root for license information.
//----------------------------------------------------------------------------------------------

#pragma once

// The Clang APIs apiary uses that differ between the LLVM majors it supports.
//
// Every version difference lives in ClangCompat.cpp, behind these functions,
// so no other file names a version-sensitive API or tests a version. Two
// rules keep it that way:
//
// - Prefer an API every supported major has, even at the cost of adapting to
//   it, over a branch per major. One code path behaves the same everywhere.
// - When a branch is unavoidable, test CLANG_VERSION_MAJOR, not
//   __has_include: a version test covers renamed functions and changed
//   signatures as well as moved headers, and says which branch becomes dead
//   when APIARY_LLVM_VERSION_MIN rises.

#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/PrettyPrinter.h"
#include "clang/AST/RawCommentList.h"
#include "llvm/ADT/SmallVector.h"

namespace apiary::clang_compat {

/// @brief The doc comment attached to @p decl, or null when it has none.
///
/// Looks on every redeclaration, so a declaration documented on another of
/// its declarations (a forward declaration whose comment is on the
/// definition, or the reverse) reports that comment.
/// @param decl The declaration to look up.
/// @param ctx The AST context owning @p decl.
/// @return The attached raw comment, or null.
[[nodiscard]] clang::RawComment const *raw_comment_for(clang::Decl const *decl, clang::ASTContext const &ctx);

/// @brief Clang's Unified Symbol Resolution string for @p decl.
/// @param decl The declaration to identify.
/// @param buf Receives the USR.
/// @return True when @p decl has no USR and should be ignored, as
///         ``clang::index::generateUSRForDecl`` reports it.
[[nodiscard]] bool generate_usr(clang::Decl const *decl, llvm::SmallVectorImpl<char> &buf);

/// @brief @p policy, adjusted so that it prints the same text on every
///        supported LLVM major.
///
/// Every printing policy apiary uses comes through here (or through
/// printing_policy()), so a printer change in a new major is corrected once.
/// @param policy The policy to adjust.
/// @return The adjusted policy.
[[nodiscard]] clang::PrintingPolicy stable_policy(clang::PrintingPolicy policy);

/// @brief A printing policy for @p ctx's language options, adjusted as by
///        stable_policy(). Callers set their own flags on the result.
/// @param ctx The AST context whose language options to use.
/// @return The policy.
[[nodiscard]] clang::PrintingPolicy printing_policy(clang::ASTContext const &ctx);

} // namespace apiary::clang_compat
