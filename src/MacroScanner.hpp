//----------------------------------------------------------------------------------------------
// Copyright (c) The Einsums Developers. All rights reserved.
// Licensed under the MIT License. See LICENSE.txt in the project root for license information.
//----------------------------------------------------------------------------------------------

#pragma once

#include <vector>

#include "IR.hpp"
#include "llvm/ADT/StringRef.h"

namespace apiary {

/// @brief Scan raw header source text for *documented* preprocessor macros.
///
/// Captures a macro when a doc comment (``/** ... */`` / ``/*! ... */`` or a run
/// of ``///``) is immediately followed by a ``#define NAME`` (optionally
/// function-like ``NAME(args)``).
///
/// @param source Raw header source text to scan.
/// @return The documented macros recovered from @p source.
///
/// @note Reads raw source text — all preprocessor branches — so a macro
///       documented inside a compiler-specific ``#if`` branch is still found
///       regardless of which branch the current build would take. Macros are
///       not AST declarations, so this is the only way to recover their
///       documentation.
/// @warning Undocumented ``#define`` directives are ignored, mirroring the "document only
///          documented entities" rule for the rest of the docs-mode surface.
std::vector<BoundMacro> scan_macros(llvm::StringRef source);

} // namespace apiary
