//----------------------------------------------------------------------------------------------
// Copyright (c) The Einsums Developers. All rights reserved.
// Licensed under the MIT License. See LICENSE.txt in the project root for license information.
//----------------------------------------------------------------------------------------------

#include "BuiltinHeaders.hpp"

#include "clang/Basic/Version.h"
#include "clang/Options/OptionUtils.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"

namespace apiary::builtin_headers {

namespace {

// Any symbol in the apiary executable. GetResourcesPath resolves the binary's
// path from it, which is how libTooling finds the default it injects.
int g_anchor = 0;

} // namespace

bool has_builtin_headers(llvm::StringRef dir) {
    if (dir.empty()) {
        return false;
    }
    llvm::SmallString<256> probe(dir);
    llvm::sys::path::append(probe, "include", "stddef.h");
    return llvm::sys::fs::exists(probe);
}

std::string default_resource_dir(char const *argv0) {
    std::string const next_to_binary = clang::GetResourcesPath(argv0, &g_anchor);
    if (has_builtin_headers(next_to_binary)) {
        return next_to_binary;
    }
#ifdef APIARY_LLVM_RESOURCE_DIR
    if (has_builtin_headers(APIARY_LLVM_RESOURCE_DIR)) {
        return APIARY_LLVM_RESOURCE_DIR;
    }
#endif
    return next_to_binary;
}

std::string resource_dir_arg(std::vector<std::string> const &args) {
    std::string found;
    for (std::size_t i = 0; i < args.size(); ++i) {
        llvm::StringRef arg(args[i]);
        if (arg == "-resource-dir" && i + 1 < args.size()) {
            found = args[++i];
        } else if (arg.consume_front("-resource-dir=")) {
            found = arg.str();
        }
    }
    // The last one wins, as it does for the driver.
    return found;
}

unsigned llvm_major() {
    return CLANG_VERSION_MAJOR;
}

} // namespace apiary::builtin_headers
