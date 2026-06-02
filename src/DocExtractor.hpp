//----------------------------------------------------------------------------------------------
// Copyright (c) The Einsums Developers. All rights reserved.
// Licensed under the MIT License. See LICENSE.txt in the project root for license information.
//----------------------------------------------------------------------------------------------

#pragma once

#include <string>

#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "llvm/ADT/StringRef.h"

namespace apiary {

/// @brief Cleans a raw comment block (``/** ... */`` or ``/// ...``) into doxygen body text.
///
/// Strips comment markers, leading ``*``, and banner lines. Shared by
/// ``extract_doc`` (AST decls) and the macro scanner (raw ``#define`` text).
///
/// @param text The raw comment block to clean.
/// @return The cleaned doxygen body text.
std::string clean_raw_comment(llvm::StringRef text);

/// @brief Returns the cleaned doxygen comment associated with `decl`.
///
/// Cleaning strips ``///``, ``/**``, ``*/``, and leading ``*`` markers but
/// otherwise preserves the text verbatim — Phase 2 keeps escaping/markdown
/// conversion out of scope (Phase 3 polish handles ``\param`` / ``\return``
/// reflow).
///
/// @param decl The declaration to extract the doc-comment from.
/// @param ctx The AST context owning @p decl.
/// @return The cleaned doxygen comment, or an empty string if the declaration has no doc-comment.
std::string extract_doc(clang::Decl const *decl, clang::ASTContext &ctx);

} // namespace apiary
