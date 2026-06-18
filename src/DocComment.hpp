//----------------------------------------------------------------------------------------------
// Copyright (c) The Einsums Developers. All rights reserved.
// Licensed under the MIT License. See LICENSE.txt in the project root for license information.
//----------------------------------------------------------------------------------------------

#pragma once

// DocComment — parse a raw Doxygen doc-comment body into structured,
// reST-ready fields.
//
// DocExtractor (DocExtractor.hpp) hands us the comment text with the ///,
// /**, */, and leading-* markers already stripped, but otherwise verbatim:
// `@brief`, `@param x ...`, `@versionadded{1.0.0}`, `@f$ ... @f$` math,
// `@code ... @endcode`, and so on. The docs renderer wants those split into
// a brief line, a detail body, and per-parameter / return / throws entries,
// with the inline and block Doxygen commands converted to reStructuredText.
//
// This parser is intentionally scoped to the command set that actually
// appears in the Einsums headers (see the grep audit in the commit that
// introduced it), not the whole of Doxygen. Unknown `@command` tokens are
// passed through with the leading marker stripped rather than dropped, so
// nothing silently vanishes.
//
// Output is consumed by DocsJson (the `doc_structured` object) and is the
// shared normalizer Option 2 (a Breathe replacement) will reuse against
// Doxygen XML doc bodies.

#include <string>
#include <vector>

namespace apiary {

/// @brief A named doc entry: @param / @tparam / @throws.
///
/// `name` is the parameter or exception-type name; `description` is reST-ready
/// text (inline commands already converted).
struct DocEntry {
    /// The parameter or exception-type name.
    std::string name;
    /// reST-ready description text (inline commands already converted).
    std::string description;
};

/// @brief Structured form of a doc comment.
///
/// Every text field is reST-ready: inline Doxygen commands (`@c`, `@p`, `@ref`,
/// `@f$math@f$`, ...) are converted and block constructs (`@code`,
/// `@versionadded`, `@note`, ...) are rendered as reST directives inside
/// `detail`.
struct DocComment {
    /// One-line summary (from @brief or leading paragraph).
    std::string           brief;
    /// Remaining prose + converted block directives.
    std::string           detail;
    /// @param entries.
    std::vector<DocEntry> params;
    /// @tparam entries (C++ template params; usually omitted from Python pages).
    std::vector<DocEntry> tparams;
    /// @return / @returns text.
    std::string           returns;
    /// @throws / @throw / @exception entries (name = exception type).
    std::vector<DocEntry> throws_;
    /// Version a symbol became available, from ``@since`` (docs-graph
    /// availability). Empty when unstated.
    std::string           since;
    /// True when the symbol carries a ``@deprecated`` tag.
    bool                  deprecated = false;
    /// The ``@deprecated`` note (migration guidance), or empty.
    std::string           deprecated_note;

    /// @brief Whether every field is empty.
    /// @return `true` if brief, detail, params, tparams, returns, throws, and availability are all empty.
    [[nodiscard]] bool empty() const {
        return brief.empty() && detail.empty() && params.empty() && tparams.empty() && returns.empty() &&
               throws_.empty() && since.empty() && !deprecated && deprecated_note.empty();
    }
};

/// @brief Parse a marker-stripped Doxygen comment body into structured form.
/// @param raw The marker-stripped Doxygen comment body.
/// @return The structured DocComment.
DocComment parse_doc_comment(std::string const &raw);

} // namespace apiary
