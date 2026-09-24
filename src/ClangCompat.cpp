//----------------------------------------------------------------------------------------------
// Copyright (c) The Einsums Developers. All rights reserved.
// Licensed under the MIT License. See LICENSE.txt in the project root for license information.
//----------------------------------------------------------------------------------------------

#include "ClangCompat.hpp"

#include "clang/Basic/Version.h"

// LLVM 23 moved USR generation out of clangIndex into its own library; the
// function and its namespace are unchanged.
#if CLANG_VERSION_MAJOR >= 23
#    include "clang/UnifiedSymbolResolution/USRGeneration.h"
#else
#    include "clang/Index/USRGeneration.h"
#endif

namespace apiary::clang_compat {

clang::RawComment const *raw_comment_for(clang::Decl const *decl, clang::ASTContext const &ctx) {
    // getRawCommentForAnyRedecl is the lookup every supported major shares.
    // LLVM 22's getRawCommentForDeclNoCache, which looked at the one
    // declaration only, is gone from 23, whose own per-declaration lookup is
    // private.
    return ctx.getRawCommentForAnyRedecl(decl);
}

bool generate_usr(clang::Decl const *decl, llvm::SmallVectorImpl<char> &buf) {
    return clang::index::generateUSRForDecl(decl, buf);
}

clang::PrintingPolicy stable_policy(clang::PrintingPolicy policy) {
    // LLVM 23 prints every expression's reference to a declaration by its
    // qualified name when FullyQualifiedName is set, which apiary sets for
    // types. For a non-type template parameter that yields
    // ``Base<T, ns::Derived::Rank>``, which is not C++, where LLVM 22 printed
    // ``Base<T, Rank>``. CleanUglifiedParameters takes parameters and
    // template parameters back to printing by name, on both majors. Its only
    // other effect, the same on both, is to print a reserved parameter name
    // without its leading underscores (libc++'s ``_Tp`` as ``Tp``).
    policy.CleanUglifiedParameters = true;
    return policy;
}

clang::PrintingPolicy printing_policy(clang::ASTContext const &ctx) {
    return stable_policy(clang::PrintingPolicy(ctx.getLangOpts()));
}

} // namespace apiary::clang_compat
