//----------------------------------------------------------------------------------------------
// Copyright (c) The Einsums Developers. All rights reserved.
// Licensed under the MIT License. See LICENSE.txt in the project root for license information.
//----------------------------------------------------------------------------------------------

#include "DocExtractor.hpp"

#include <algorithm>
#include <array>
#include <string>
#include <vector>

#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/RawCommentList.h"
#include "llvm/ADT/StringRef.h"

namespace apiary {

namespace {

// Strip a leading ``///``, ``//!``, ``/**``, or ``*`` from a single line of a
// doxygen comment. Prefixes are checked in length-descending order so ``///``
// matches before ``//``.
//
// Whatever follows the marker is kept verbatim, including its leading spaces.
// This used to return line.trim(), which is why every construct that IS its
// indentation came out wrong: a nested bullet list flattened to one level, a
// definition list collapsed into a paragraph, and a directive spliced through
// @rst lost the body that made it a directive. clean_raw_comment below removes
// the common indent, so the marker's own trailing space costs nothing and the
// author's relative indentation survives.
llvm::StringRef strip_comment_markers(llvm::StringRef line) {
    static constexpr std::array<llvm::StringLiteral, 7> k_prefixes = {
        llvm::StringLiteral{"///"}, llvm::StringLiteral{"//!"}, llvm::StringLiteral{"/**"}, llvm::StringLiteral{"/*!"},
        llvm::StringLiteral{"//"},  llvm::StringLiteral{"/*"},  llvm::StringLiteral{"*"},
    };
    line = line.ltrim();
    for (auto const &prefix : k_prefixes) {
        if (line.starts_with(prefix)) {
            line = line.drop_front(prefix.size());
            break;
        }
    }
    if (line.ends_with("*/")) {
        line = line.drop_back(2);
    }
    return line.rtrim();
}

// True for "banner" rows of repeating decoration — e.g.
// ``///////////////////////////``, ``****************/``, ``=========``,
// ``-------``. These add no documentation value and otherwise survive
// marker stripping (a row of slashes only loses the first ``///``).
// We treat them as empty lines so leading/trailing trims drop them and
// interior banners collapse to a blank line.
bool is_banner_line(llvm::StringRef line) {
    if (line.empty()) {
        return false;
    }
    for (char const c : line) {
        if (c != '/' && c != '*' && c != '=' && c != '-' && c != '_' && c != '#' && c != ' ' && c != '\t') {
            return false;
        }
    }
    return true;
}

} // namespace

std::string extract_doc(clang::Decl const *decl, clang::ASTContext &ctx) {
    clang::RawComment const *raw = ctx.getRawCommentForDeclNoCache(decl);
    if (raw == nullptr) {
        // For templated entities the doc comment is attached to the describing
        // TemplateDecl, not the inner pattern decl — e.g. a member function
        // template's comment lives on its FunctionTemplateDecl, not the
        // CXXMethodDecl the visitor walks. Retry through the describing template.
        if (clang::TemplateDecl const *td = decl->getDescribedTemplate()) {
            raw = ctx.getRawCommentForDeclNoCache(td);
        }
    }
    if (raw == nullptr) {
        return "";
    }
    return clean_raw_comment(raw->getRawText(ctx.getSourceManager()));
}

std::string clean_raw_comment(llvm::StringRef text) {
    std::vector<std::string> lines;
    llvm::StringRef          remaining = text;
    while (!remaining.empty()) {
        auto [head, tail]       = remaining.split('\n');
        llvm::StringRef cleaned = strip_comment_markers(head);
        if (is_banner_line(cleaned)) {
            cleaned = llvm::StringRef{};
        }
        if (!cleaned.empty() || !lines.empty()) {
            lines.emplace_back(cleaned.str());
        }
        if (tail.data() == nullptr) {
            break;
        }
        remaining = tail;
    }
    while (!lines.empty() && lines.back().empty()) {
        lines.pop_back();
    }

    // Remove the indentation every line shares - one space after ``*`` in the
    // common ``/** ... */`` shape - so column 0 means column 0 and everything
    // deeper keeps its offset from it. Blank lines carry no indent and must not
    // drag the minimum to zero.
    std::size_t common = std::string::npos;
    for (std::string const &l : lines) {
        if (l.empty()) {
            continue;
        }
        std::size_t const indent = l.find_first_not_of(" \t");
        if (indent == std::string::npos) {
            continue; // whitespace-only; rtrim above makes this rare
        }
        common = std::min(common, indent);
    }
    if (common != std::string::npos && common > 0) {
        for (std::string &l : lines) {
            l.erase(0, std::min(common, l.size()));
        }
    }
    // ``///<`` / ``/**<`` / ``//!<`` document the PRECEDING member. The ``<``
    // is Doxygen's marker for that, never content, so drop it here - at the
    // one place both consumers read from. parse_doc_comment strips it too,
    // which is why docs mode always looked right while the binding emitter,
    // which uses this raw text as the docstring, emitted "< C order.".
    if (!lines.empty() && !lines.front().empty() && lines.front().front() == '<') {
        lines.front().erase(0, 1);
        std::size_t const first = lines.front().find_first_not_of(" \t");
        lines.front().erase(0, first == std::string::npos ? lines.front().size() : first);
    }
    std::string out;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (i != 0) {
            out += '\n';
        }
        out += lines[i];
    }
    return out;
}

} // namespace apiary
