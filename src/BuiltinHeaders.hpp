//----------------------------------------------------------------------------------------------
// Copyright (c) The Einsums Developers. All rights reserved.
// Licensed under the MIT License. See LICENSE.txt in the project root for license information.
//----------------------------------------------------------------------------------------------

// Where apiary's libclang finds Clang's builtin headers (stddef.h, stdarg.h,
// the intrinsics): the "resource directory".
//
// They have to match libclang's own version. The intrinsics headers call
// compiler builtins by name, and a header from another major can name one this
// libclang does not have, so borrowing another Clang's headers fails in ways
// that look like the user's code is wrong.

#pragma once

#include <string>
#include <vector>

#include "llvm/ADT/StringRef.h"

namespace apiary::builtin_headers {

/// The resource directory apiary uses when a command line names none.
///
/// libclang's default sits next to the apiary binary, in
/// ``<bindir>/../lib/clang/<major>``, which is where a conda environment that
/// installs ``clang-<major>`` puts it. When that has no headers (an install
/// beside an LLVM that lives elsewhere, or a build tree), the resource
/// directory of the LLVM apiary was built against, if it has them. Otherwise
/// the libclang default, so a diagnostic can name where they were expected.
[[nodiscard]] std::string default_resource_dir(char const *argv0);

/// True when @p dir holds Clang's builtin headers.
[[nodiscard]] bool has_builtin_headers(llvm::StringRef dir);

/// The ``-resource-dir`` a compiler command line passes (either spelling), or
/// empty when it passes none.
[[nodiscard]] std::string resource_dir_arg(std::vector<std::string> const &args);

/// The LLVM major apiary's libclang belongs to.
[[nodiscard]] unsigned llvm_major();

} // namespace apiary::builtin_headers
