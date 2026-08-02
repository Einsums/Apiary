//----------------------------------------------------------------------------------------------
// Copyright (c) The Einsums Developers. All rights reserved.
// Licensed under the MIT License. See LICENSE.txt in the project root for license information.
//----------------------------------------------------------------------------------------------

#include "DocComment.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <regex>
#include <string>
#include <unordered_set>
#include <vector>

namespace apiary {

namespace {

// ── small string helpers ────────────────────────────────────────────────

std::string ltrim(std::string s) {
    std::size_t i = 0;
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) {
        ++i;
    }
    return s.substr(i);
}

std::string rtrim(std::string s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
        s.pop_back();
    }
    return s;
}

std::string trim(std::string s) {
    return rtrim(ltrim(std::move(s)));
}

bool is_blank(std::string const &s) {
    return trim(s).empty();
}

// Indent every non-empty line of `body` by `pad` (for reST directive bodies).
std::string indent_block(std::string const &body, std::string const &pad) {
    std::string out;
    std::size_t start = 0;
    bool        first = true;
    auto        emit  = [&](std::string const &line) {
        if (!first) {
            out += "\n";
        }
        first = false;
        if (line.empty()) {
            return; // keep blank lines blank (no trailing pad)
        }
        out += pad + line;
    };
    for (std::size_t i = 0; i <= body.size(); ++i) {
        if (i == body.size() || body[i] == '\n') {
            emit(body.substr(start, i - start));
            start = i + 1;
        }
    }
    return out;
}

// ── inline command conversion (@c, @p, @ref, @a, @b → reST) ──────────────

std::string convert_inline(std::string s) {
    // Each pattern matches an `@cmd word` / `\cmd word` and wraps the word.
    // `@c`/`@p` also absorb a trailing template-argument list (`@c
    // std::complex<double>` → ``std::complex<double>``) and subscript (`@p
    // extents[k]` → ``extents[k]``) so the closing ``-literal isn't
    // immediately followed by `<` or `[`, which reST rejects as a dangling
    // inline-literal start-string.
    static std::regex const re_code(R"([@\\][cp]\s+([A-Za-z_][A-Za-z0-9_:.]*(?:<[^>]*>)?(?:\(\))?(?:\[[^\]]*\])?))");
    static std::regex const re_ref(R"([@\\]ref\s+([A-Za-z_][A-Za-z0-9_:.]*(?:\(\))?))");
    // Absorb a trailing ``()`` into the emphasised span (like @c/@p/@ref above)
    // so the closing ``*`` isn't immediately followed by ``(`` — which reST
    // rejects as an emphasis end-string (``*foo*()`` → unterminated emphasis).
    static std::regex const re_emph(R"([@\\][ae]\s+([A-Za-z_][A-Za-z0-9_:.]*(?:\(\))?))");
    static std::regex const re_bold(R"([@\\]b\s+([A-Za-z_][A-Za-z0-9_:.]*))");
    s = std::regex_replace(s, re_code, "``$1``");
    s = std::regex_replace(s, re_ref, "``$1``");
    s = std::regex_replace(s, re_emph, "*$1*");
    s = std::regex_replace(s, re_bold, "**$1**");
    return s;
}

// ── inline HTML ─────────────────────────────────────────────────────────
//
// Doxygen accepts a little inline HTML and reST accepts none, so a `<tt>`
// reached the reader as four visible characters. Convert the paired inline
// tags Doxygen documents; leave anything else exactly as written.
//
// Three rules, and each of them is something a real comment in this corpus
// needs:
//
//  - Only a PAIR converts, each opening tag taking the next closing one of
//    the same name. `std::get<i>(tuple)` in prose names no markup, and a lone
//    `<b>` is far likelier to be part of a spec string than an opening tag.
//    Leftovers are left alone one at a time, so a stray tag does not stop a
//    real pair beside it from converting.
//  - Text inside backticks is copied through untouched, because
//    ``"<output> <- <a> ; <b>"`` is a real doc string here and turning its
//    `<b>` into `**` would cut the literal in half.
//  - reST does not recognise inline markup that touches a word character, so
//    `cdot<b>u</b>` has to become ``cdot\ **u**\ .`` to render as bold at
//    all. Without the escape the asterisks are what the reader sees.
struct HtmlTag {
    char const *name;
    char const *delim;
};

std::vector<HtmlTag> const &html_tags() {
    static std::vector<HtmlTag> const tags = {
        {"tt", "``"}, {"code", "``"}, {"b", "**"}, {"strong", "**"}, {"i", "*"}, {"em", "*"},
    };
    return tags;
}

bool is_word_char(char c) {
    return (std::isalnum(static_cast<unsigned char>(c)) != 0) || c == '_';
}

// Does `s` hold `<name>` (or `</name>`) at `i`, case-insensitively?
bool tag_at(std::string const &s, std::size_t i, char const *name, bool closing) {
    std::size_t p = i;
    if (s[p] != '<') {
        return false;
    }
    ++p;
    if (closing) {
        if (p >= s.size() || s[p] != '/') {
            return false;
        }
        ++p;
    } else if (p < s.size() && s[p] == '/') {
        return false;
    }
    for (char const *n = name; *n != '\0'; ++n, ++p) {
        if (p >= s.size() || std::tolower(static_cast<unsigned char>(s[p])) != *n) {
            return false;
        }
    }
    return p < s.size() && s[p] == '>';
}

std::string convert_html(std::string const &s) {
    struct Hit {
        std::size_t pos     = 0;
        std::size_t len     = 0;
        std::size_t tag     = 0;
        bool        closing = false;
        bool        paired  = false;
    };
    std::vector<Hit> hits;

    for (std::size_t i = 0; i < s.size();) {
        if (s[i] == '`') { // skip a backtick-fenced run whole
            std::size_t const ticks = (s.compare(i, 2, "``") == 0) ? 2 : 1;
            std::string const fence(ticks, '`');
            if (std::size_t const close = s.find(fence, i + ticks); close != std::string::npos) {
                i = close + ticks;
                continue;
            }
        }
        bool matched = false;
        if (s[i] == '<') {
            for (std::size_t t = 0; t < html_tags().size() && !matched; ++t) {
                for (bool const closing : {false, true}) {
                    if (!tag_at(s, i, html_tags()[t].name, closing)) {
                        continue;
                    }
                    std::size_t const len = std::strlen(html_tags()[t].name) + (closing ? 3 : 2);
                    hits.push_back({i, len, t, closing, false});
                    i += len;
                    matched = true;
                    break;
                }
            }
        }
        if (!matched) {
            ++i;
        }
    }

    // Pair each opening tag with the next closing one of the same name. What
    // is left over is not markup and is not touched: one stray `<b>` in a spec
    // string must not stop the real `<b>...</b>` beside it from converting.
    std::vector<std::size_t> pending(html_tags().size(), std::string::npos);
    for (std::size_t k = 0; k < hits.size(); ++k) {
        std::size_t &open_at = pending[hits[k].tag];
        if (!hits[k].closing) {
            open_at = k;
            continue;
        }
        if (open_at == std::string::npos) {
            continue;
        }
        // An empty span has no reST spelling - ```` `` `` ```` is not an
        // inline literal - so leave the tags as prose instead.
        std::size_t const from = hits[open_at].pos + hits[open_at].len;
        if (!trim(s.substr(from, hits[k].pos - from)).empty()) {
            hits[open_at].paired = true;
            hits[k].paired       = true;
        }
        open_at = std::string::npos;
    }

    std::string out;
    std::size_t copied = 0;
    for (auto const &h : hits) {
        if (!h.paired) {
            continue;
        }
        out.append(s, copied, h.pos - copied);
        std::size_t after = h.pos + h.len;
        if (h.closing) {
            // reST reads a delimiter followed (or preceded) by a space as
            // ordinary text, so `<tt> cto / cfrom </tt>` has to lose the
            // padding the author put inside the tags.
            while (!out.empty() && (out.back() == ' ' || out.back() == '\t')) {
                out.pop_back();
            }
        }
        if (!h.closing && h.pos > 0 && is_word_char(s[h.pos - 1])) {
            out += "\\ ";
        }
        out += html_tags()[h.tag].delim;
        if (h.closing && after < s.size() && is_word_char(s[after])) {
            out += "\\ ";
        }
        if (!h.closing) {
            while (after < s.size() && (s[after] == ' ' || s[after] == '\t')) {
                ++after;
            }
        }
        copied = after;
    }
    out.append(s, copied, std::string::npos);
    return out;
}

// Collapse every run of whitespace (newlines included) to a single space.
std::string collapse_whitespace(std::string const &s) {
    std::string out;
    bool        in_space = false;
    for (char const c : s) {
        if (std::isspace(static_cast<unsigned char>(c)) != 0) {
            in_space = true;
            continue;
        }
        if (in_space && !out.empty()) {
            out += ' ';
        }
        in_space = false;
        out += c;
    }
    return out;
}

// ── span protection (math / code / verbatim / embedded rst) ──────────────
//
// These spans hold LaTeX backslashes or pre-formatted text that must not be
// touched by the line/inline command processing. We pull each out, convert
// it to reST, and leave a placeholder that is spliced back at the very end.

constexpr char k_ph_open  = '\x01';
constexpr char k_ph_close = '\x02';

struct ProtectedSpan {
    std::string replacement;   // reST-ready text spliced back at the end
    bool        block = false; // true → ensure blank-line separation
};

std::string make_placeholder(std::size_t index) {
    return std::string(1, k_ph_open) + std::to_string(index) + std::string(1, k_ph_close);
}

// Find `open` then the next `close` after it. Returns npos if unpaired.
std::size_t find_close(std::string const &s, std::size_t after, std::string const &close) {
    return s.find(close, after);
}

std::string protect_spans(std::string s, std::vector<ProtectedSpan> &spans) {
    struct Delim {
        std::string open;
        std::string close;
        char        kind; // 'i' inline-math, 'b' block-math, 'c' code, 'v' verbatim, 'r' rst
    };
    // Order matters: try the longer / block delimiters before `@f$`.
    static std::vector<Delim> const delims = {
        {"@f[", "@f]", 'b'},        {"\\f[", "\\f]", 'b'},    {"@f{", "@f}", 'b'},
        {"\\f{", "\\f}", 'b'},      {"@f$", "@f$", 'i'},      {"\\f$", "\\f$", 'i'},
        {"@code", "@endcode", 'c'}, {"@rst", "@endrst", 'r'}, {"@verbatim", "@endverbatim", 'v'},
    };
    std::string out;
    std::size_t i = 0;
    while (i < s.size()) {
        // Inside an inline literal, a command marker is prose ABOUT the command,
        // not the command. Documentation of a span feature has to name it -
        // ``@rst`` in a brief - and scanning raw text meant that opener paired
        // with the genuine terminator further down and swallowed everything
        // between them. Copy the literal through untouched so it cannot open a
        // span. A backtick run with no partner is left to the normal path.
        if (s[i] == '`') {
            std::size_t const ticks = (s.compare(i, 2, "``") == 0) ? 2 : 1;
            std::string const fence(ticks, '`');
            if (std::size_t const close = s.find(fence, i + ticks); close != std::string::npos) {
                out.append(s, i, close + ticks - i);
                i = close + ticks;
                continue;
            }
        }
        bool matched = false;
        for (auto const &d : delims) {
            if (s.compare(i, d.open.size(), d.open) != 0) {
                continue;
            }
            std::size_t const body_start = i + d.open.size();
            std::size_t const close_at   = find_close(s, body_start, d.close);
            if (close_at == std::string::npos) {
                continue; // unpaired — treat literally
            }
            std::string const inner = s.substr(body_start, close_at - body_start);
            ProtectedSpan     span;
            if (d.kind == 'i') {
                // An INLINE span is one expression. Authors wrap long ones
                // across source lines and indent the continuation, and reST
                // cannot carry an indented continuation inside an inline role -
                // it reads the role as unterminated and the next line as a
                // block quote. Interior whitespace here is cosmetic by
                // construction, so flatten it. (The block forms below are the
                // opposite: their layout is the content.)
                span.replacement = ":math:`" + collapse_whitespace(trim(inner)) + "`";
            } else if (d.kind == 'b') {
                span.replacement = "\n\n.. math::\n\n" + indent_block(trim(inner), "   ") + "\n";
                span.block       = true;
            } else if (d.kind == 'c') {
                // @code or @code{.lang}: strip an optional {.lang} right
                // after. Default to "text" (not "cpp") — these are Python
                // binding docs whose examples are usually Python or shell,
                // and "text" never trips Pygments' highlighter.
                std::string lang = "text";
                std::string code = inner;
                if (!code.empty() && code.front() == '{') {
                    std::size_t const brace = code.find('}');
                    if (brace != std::string::npos) {
                        std::string tag = code.substr(1, brace - 1); // e.g. ".py"
                        if (!tag.empty() && tag.front() == '.') {
                            lang = tag.substr(1);
                        }
                        code = code.substr(brace + 1);
                    }
                }
                span.replacement = "\n\n.. code-block:: " + lang + "\n\n" + indent_block(trim(code), "   ") + "\n";
                span.block       = true;
            } else if (d.kind == 'v') {
                span.replacement = "\n\n::\n\n" + indent_block(trim(inner), "   ") + "\n";
                span.block       = true;
            } else { // 'r' — already reST, splice verbatim
                span.replacement = "\n\n" + trim(inner) + "\n";
                span.block       = true;
            }
            out += make_placeholder(spans.size());
            spans.push_back(std::move(span));
            i       = close_at + d.close.size();
            matched = true;
            break;
        }
        if (!matched) {
            out += s[i++];
        }
    }
    return out;
}

std::string restore_spans(std::string s, std::vector<ProtectedSpan> const &spans) {
    std::string out;
    for (std::size_t i = 0; i < s.size();) {
        if (s[i] == k_ph_open) {
            std::size_t const close = s.find(k_ph_close, i);
            if (close != std::string::npos) {
                std::size_t const idx = std::stoul(s.substr(i + 1, close - i - 1));
                if (idx < spans.size()) {
                    out += spans[idx].replacement;
                }
                i = close + 1;
                continue;
            }
        }
        out += s[i++];
    }
    return out;
}

// ── block command parsing ────────────────────────────────────────────────

// Commands whose entire line is dropped: entity-kind markers that merely
// restate the declaration, and grouping directives with no doc value.
std::unordered_set<std::string> const &drop_commands() {
    static std::unordered_set<std::string> const s = {
        "class",     "struct",   "def",  "typedef",    "var",     "property",    "concept",    "fn",       "function",
        "namespace", "enum",     "file", "headerfile", "relates", "relatesalso", "ingroup",    "defgroup", "addtogroup",
        "weakgroup", "memberof", "name", "interface",  "union",   "extends",     "implements", "details",
    };
    return s;
}

// Commands that mark up a *word*, not a block. The distinction matters at the
// start of a line: the block-command loop below claims any line whose first
// token begins with `@`, but an author who wraps a sentence has no control over
// which token lands first, and an inline command there is still inline. Claimed
// as a block it would be stripped of its markup by convert_inline never seeing
// it, and - worse - would break the enclosing bullet item, because the loop
// treats a command line as the end of a list.
//
// The set is exactly what convert_inline handles, and must stay that way: a
// command listed here and not converted there would survive into the output as
// a literal `@cmd`. match_command lowercases and takes the maximal run of
// letters, so both the `@` and `\` spellings arrive here, and `@brief` is never
// mistaken for `@b`.
bool is_inline_command(std::string const &cmd) {
    return cmd == "c" || cmd == "p" || cmd == "ref" || cmd == "a" || cmd == "b" || cmd == "e";
}

// Admonition mapping for @note-style commands → reST directive name.
std::string admonition_for(std::string const &cmd) {
    if (cmd == "note" || cmd == "remark" || cmd == "remarks") {
        return "note";
    }
    if (cmd == "warning") {
        return "warning";
    }
    if (cmd == "attention") {
        return "attention";
    }
    if (cmd == "see" || cmd == "sa" || cmd == "seealso") {
        return "seealso";
    }
    if (cmd == "pre" || cmd == "post") {
        return "admonition"; // rendered with an explicit Pre/Postcondition title
    }
    return {};
}

// Parse a `@cmd[{arg}] rest` line. Returns true if `stripped` begins with a
// command. Sets cmd (letters, lowercased), brace_arg, and rest.
bool match_command(std::string const &stripped, std::string &cmd, std::string &brace_arg, std::string &rest) {
    if (stripped.size() < 2 || (stripped[0] != '@' && stripped[0] != '\\')) {
        return false;
    }
    std::size_t i = 1;
    while (i < stripped.size() && std::isalpha(static_cast<unsigned char>(stripped[i]))) {
        ++i;
    }
    if (i == 1) {
        return false; // lone @ or backslash
    }
    cmd.assign(stripped, 1, i - 1);
    for (char &c : cmd) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    brace_arg.clear();
    if (i < stripped.size() && stripped[i] == '{') {
        std::size_t const close = stripped.find('}', i);
        if (close != std::string::npos) {
            brace_arg = stripped.substr(i + 1, close - i - 1);
            i         = close + 1;
        }
    }
    rest = trim(stripped.substr(i));
    return true;
}

// Pull the leading name (and optional Doxygen [in]/[out] direction) off a
// @param/@tparam/@throws payload, returning {name, description}.
DocEntry parse_named(std::string const &payload) {
    std::string s = ltrim(payload);
    if (!s.empty() && s.front() == '[') { // @param[in] x ...
        std::size_t const close = s.find(']');
        if (close != std::string::npos) {
            s = ltrim(s.substr(close + 1));
        }
    }
    std::size_t i = 0;
    while (i < s.size() && !std::isspace(static_cast<unsigned char>(s[i]))) {
        ++i;
    }
    DocEntry e;
    e.name        = s.substr(0, i);
    e.description = trim(s.substr(i));
    return e;
}

// Accumulator that renders @note / @versionchangeddesc / @par bodies into
// detail with proper reST blank-line separation.
struct Pending {
    bool                     active = false;
    std::string              directive; // reST directive name, "version*", or "par"
    std::string              arg;       // version number or @par title
    std::vector<std::string> body;
};

// Start a new paragraph/block in `detail`, guaranteeing exactly one blank
// line of separation regardless of how `detail` currently ends.
void append_paragraph(std::string &detail, std::string const &text) {
    if (text.empty()) {
        return;
    }
    if (!detail.empty()) {
        while (!detail.empty() && detail.back() == '\n') {
            detail.pop_back();
        }
        detail += "\n\n";
    }
    detail += text;
}

// ── block structure ─────────────────────────────────────────────────────
//
// Prose in a doc comment is indentation-bearing, because reST reads every
// column structurally, and the two readings of an indented line cannot be
// told apart one line at a time:
//
//   - a NESTED list item, a definition body, a second paragraph inside a
//     list item - the indent is the structure, and dropping it flattens the
//     document (Finding 4: a nested list became one level, a definition list
//     became a paragraph);
//   - a wrapped sentence's hanging indent, a right-aligned enumerator where
//     ``2.`` sits one column deeper than ``10.``, a continuation aligned
//     under ``@note`` - the indent is cosmetic, and preserving it makes reST
//     report a block quote that ends without a blank line.
//
// Both readings are common, and the second is much the commoner of the two
// in real headers, which is why the naive "keep what the author wrote" fix
// was tried and reverted. What distinguishes them is not the line, it is the
// block the line sits in: an indent counts as nesting when it clears the
// enclosing item's TEXT column, and a dedent that lands between two open
// levels is a sibling of the nearer one rather than a new level.
//
// So these helpers do the one thing a per-line loop cannot: they carry the
// enclosing block structure. Indents are then re-emitted at canonical
// columns rather than the author's, so a list is properly nested and a
// right-aligned enumerator is normalised to one level.

// Where the TEXT of a list item starts, measured from the (already
// unindented) start of the item - 2 for "- ", 4 for "10. " - or 0 when `s`
// does not begin an item at all. That column is what reST requires an item's
// continuation and any nested list to reach, so it is the number the nesting
// decision is made against.
std::size_t list_marker_width(std::string const &s) {
    std::size_t i = 0;
    if (!s.empty() && (s[0] == '-' || s[0] == '+' || s[0] == '*')) {
        i = 1;
    } else {
        while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
            ++i;
        }
        if (i == 0 || i >= s.size() || (s[i] != '.' && s[i] != ')')) {
            return 0;
        }
        ++i;
    }
    if (i >= s.size() || s[i] != ' ') {
        return 0; // "**bold", "-flag", "1.5"
    }
    while (i < s.size() && s[i] == ' ') {
        ++i;
    }
    return i;
}

struct SourceLine {
    std::size_t indent = 0;
    std::string text; // indentation stripped; empty on a blank line
    bool        blank = true;
};

std::vector<SourceLine> to_source_lines(std::vector<std::string> const &raw) {
    std::vector<SourceLine> out;
    out.reserve(raw.size());
    for (auto const &r : raw) {
        SourceLine  sl;
        std::size_t i = 0;
        while (i < r.size() && (r[i] == ' ' || r[i] == '\t')) {
            sl.indent += (r[i] == '\t') ? 4 : 1;
            ++i;
        }
        sl.text  = rtrim(r.substr(i));
        sl.blank = sl.text.empty();
        out.push_back(std::move(sl));
    }
    return out;
}

// One open list item: where the author put it, and where we are putting it.
struct ListLevel {
    std::size_t marker_col = 0; // author's column for the marker
    std::size_t text_col   = 0; // author's column for the item's text
    std::size_t emit_col   = 0; // our column for the marker
    std::size_t emit_text  = 0; // our column for this item's text (children go here)
};

// Is the paragraph starting at `first` a definition list?
//
// A definition list and a hanging-indent wrap are the same characters: a line
// at the base indent, then a line deeper. Two things tell them apart, and
// both are required here.
//
// A term must HAVE a body: every base-indent line is followed by a deeper
// one. A wrapped sentence that returns to the base indent ("...continues on
// the next line" after an indented fragment) fails this, which matters
// because that is exactly the shape docutils rejects.
//
// And there must be at least TWO term/body pairs. One pair on its own is
// character-identical to a wrap whose indented fragment happens to end the
// paragraph - and in this corpus that wrap is roughly ten times commoner, so
// a single pair is left to the flatten-and-join path it already takes today.
// An alternation, on the other hand, cannot be a wrap: nobody wraps a
// sentence by indenting, dedenting and indenting again.
bool paragraph_is_definition_list(std::vector<SourceLine> const &lines, std::size_t first) {
    std::size_t const base  = lines[first].indent;
    std::size_t       terms = 0;
    for (std::size_t i = first; i < lines.size() && !lines[i].blank; ++i) {
        if (list_marker_width(lines[i].text) != 0) {
            return false; // a list lives here; the list machinery owns it
        }
        if (lines[i].indent > base) {
            continue;
        }
        if (lines[i].indent < base) {
            return false;
        }
        if (i + 1 >= lines.size() || lines[i + 1].blank || lines[i + 1].indent <= base) {
            return false;
        }
        ++terms;
    }
    return terms >= 2;
}

// The distinct indents of the paragraph starting at `first`, ascending. A
// definition body is re-emitted at four spaces per RANK rather than at the
// author's column, so relative depth survives and absolute columns do not.
std::vector<std::size_t> paragraph_indents(std::vector<SourceLine> const &lines, std::size_t first) {
    std::vector<std::size_t> cols;
    for (std::size_t i = first; i < lines.size() && !lines[i].blank; ++i) {
        if (std::find(cols.begin(), cols.end(), lines[i].indent) == cols.end()) {
            cols.push_back(lines[i].indent);
        }
    }
    std::sort(cols.begin(), cols.end());
    return cols;
}

// Collapse runs of 3+ newlines down to a single blank line.
std::string collapse_blank_lines(std::string const &s) {
    std::string out;
    int         run = 0;
    for (char const c : s) {
        if (c == '\n') {
            if (++run <= 2) {
                out += c;
            }
        } else {
            run = 0;
            out += c;
        }
    }
    return out;
}

// Is `text` a whole line that is nothing but one BLOCK span's placeholder?
// Such a line stands for a ``.. math::`` or ``.. code-block::`` that brings
// its own blank-line separation, so it ends whatever block precedes it
// instead of being folded into it.
bool is_block_span(std::string const &text, std::vector<ProtectedSpan> const &spans) {
    if (text.size() < 3 || text.front() != k_ph_open || text.back() != k_ph_close) {
        return false;
    }
    std::string const digits = text.substr(1, text.size() - 2);
    if (digits.find_first_not_of("0123456789") != std::string::npos) {
        return false;
    }
    std::size_t const idx = std::stoul(digits);
    return idx < spans.size() && spans[idx].block;
}

// How many inline-literal delimiters the line carries. An odd count leaves a
// literal open across the line break, and what follows is literal text rather
// than markup - which matters because an author wrapping ``C = a * B + c *
// D`` can land a line on ``* D``, and that is not a bullet.
std::size_t count_literal_delims(std::string const &s) {
    std::size_t count = 0;
    for (std::size_t i = 0; i + 1 < s.size();) {
        if (s[i] == '`' && s[i + 1] == '`') {
            ++count;
            i += 2;
        } else {
            ++i;
        }
    }
    return count;
}

// Render a run of doc-comment prose lines - indentation as the author wrote
// it - as reST, carrying the block structure described above.
//
// Both places that hold prose go through here: the detail sink and the body
// of a ``@par`` / ``@note`` directive. They had two different, differently
// incomplete versions of this before.
std::string structure_block(std::vector<std::string> const &raw, std::vector<ProtectedSpan> const &spans) {
    std::vector<SourceLine> const lines = to_source_lines(raw);

    std::vector<std::string> out;
    auto                     push_blank = [&out] {
        if (!out.empty() && !out.back().empty()) {
            out.emplace_back();
        }
    };
    auto push_line = [&out](std::size_t col, std::string const &text) { out.push_back(std::string(col, ' ') + text); };
    // A wrapped line joins the one it continues: reST would need it at the
    // enclosing item's text column, and one logical line renders identically
    // whatever the author's continuation indent was.
    auto join_last = [&out](std::string const &text) {
        if (out.empty() || out.back().empty()) {
            out.push_back(text);
            return;
        }
        if (out.back().back() != ' ') {
            out.back() += ' ';
        }
        out.back() += text;
    };

    std::vector<ListLevel>   stack;
    std::vector<std::size_t> def_cols; // indents of the definition list being emitted
    bool                     blank_seen   = false;
    bool                     in_para      = false;
    bool                     in_def       = false;
    bool                     literal_open = false;

    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (lines[i].blank) {
            blank_seen   = true;
            literal_open = false; // a blank line ends the paragraph, and with it any literal
            continue;
        }
        std::size_t const  indent = lines[i].indent;
        std::string const &text   = lines[i].text;

        bool const in_literal = literal_open;
        literal_open          = (literal_open != (count_literal_delims(text) % 2 != 0));

        if (is_block_span(text, spans)) {
            stack.clear();
            in_para      = false;
            in_def       = false;
            blank_seen   = false;
            literal_open = false;
            push_blank();
            push_line(0, text);
            continue;
        }

        if (std::size_t const width = in_literal ? 0 : list_marker_width(text); width != 0) {
            // Where does this item belong? Clearing the enclosing item's text
            // column makes it a child; landing short of that but at or past
            // the marker makes it a sibling, which is what normalises a
            // right-aligned enumerator; anything shallower closes levels.
            std::size_t emit_col = 0;
            while (!stack.empty()) {
                if (indent >= stack.back().text_col) {
                    emit_col = stack.back().emit_text;
                    break;
                }
                if (indent >= stack.back().marker_col) {
                    emit_col = stack.back().emit_col;
                    stack.pop_back();
                    break;
                }
                stack.pop_back();
            }
            // A list must be set off from a preceding paragraph. Nesting and
            // dedenting need no blank line of their own - the columns say it.
            if (in_para || blank_seen) {
                push_blank();
            }
            in_para    = false;
            in_def     = false;
            blank_seen = false;
            push_line(emit_col, text);
            stack.push_back({indent, indent + width, emit_col, emit_col + width});
            continue;
        }

        if (!stack.empty()) {
            if (!blank_seen) {
                join_last(text); // a wrapped item
                continue;
            }
            if (indent >= stack.back().text_col) {
                // A second paragraph inside the item, which stays in the item.
                push_blank();
                push_line(stack.back().emit_text, text);
                blank_seen = false;
                continue;
            }
            stack.clear(); // the list ends here
        }

        if (!in_para || blank_seen) {
            push_blank();
            in_para    = true;
            in_def     = paragraph_is_definition_list(lines, i);
            def_cols   = in_def ? paragraph_indents(lines, i) : std::vector<std::size_t>{};
            blank_seen = false;
            push_line(0, text);
            continue;
        }

        if (in_def) {
            auto const at = std::find(def_cols.begin(), def_cols.end(), indent);
            push_line(4 * static_cast<std::size_t>(at - def_cols.begin()), text);
        } else {
            // A paragraph's lines flow together in reST, so the author's wrap
            // is kept as written and only the indent is dropped. That is what
            // makes a hanging indent harmless: it stops being an indent.
            push_line(0, text);
        }
    }

    std::string res;
    for (auto const &l : out) {
        if (!res.empty()) {
            res += "\n";
        }
        res += l;
    }
    return trim(res);
}

void flush_pending(std::string &detail, Pending &p, std::vector<ProtectedSpan> const &spans) {
    if (!p.active) {
        return;
    }
    std::string const body = structure_block(p.body, spans);

    std::string block;
    if (p.directive == "par") {
        if (!p.arg.empty()) {
            block = "**" + p.arg + "**";
            if (!body.empty()) {
                block += "\n\n" + body;
            }
        } else {
            block = body;
        }
    } else if (p.directive.rfind("version", 0) == 0) {
        block = ".. " + p.directive + ":: " + p.arg;
        if (!body.empty()) {
            block += "\n\n" + indent_block(body, "   ");
        }
    } else if (p.directive == "admonition") { // @pre / @post
        std::string const title = p.arg.empty() ? std::string{"Precondition"} : p.arg;
        block                   = ".. admonition:: " + title + "\n\n" + indent_block(body.empty() ? std::string{} : body, "   ");
    } else { // note / warning / attention / seealso
        block = ".. " + p.directive + "::";
        if (!body.empty()) {
            block += "\n\n" + indent_block(body, "   ");
        }
    }
    append_paragraph(detail, block);
    p = Pending{};
}

} // namespace

DocComment parse_doc_comment(std::string const &raw) {
    DocComment  doc;
    std::string cleaned = trim(raw);
    if (cleaned.empty()) {
        return doc;
    }
    // Doxygen `///<` / `/**<` document the *preceding* member; DocExtractor
    // leaves the leading `<`. Drop it so it doesn't appear as prose.
    if (cleaned.front() == '<') {
        cleaned = ltrim(cleaned.substr(1));
    }

    std::vector<ProtectedSpan> spans;
    // Spans come out first, so an HTML-looking string inside a @code example
    // is already gone before convert_html looks at anything.
    std::string const protectedText = convert_html(protect_spans(cleaned, spans));

    // Split into lines.
    std::vector<std::string> lines;
    {
        std::size_t start = 0;
        for (std::size_t i = 0; i <= protectedText.size(); ++i) {
            if (i == protectedText.size() || protectedText[i] == '\n') {
                lines.push_back(protectedText.substr(start, i - start));
                start = i + 1;
            }
        }
    }

    enum class Sink : std::uint8_t { Detail, Brief, Param, TParam, Return, Throw, Deprecated };
    Sink    sink = Sink::Detail;
    Pending pending;
    bool    saw_brief = false;

    // Detail prose is buffered with its indentation and rendered as one block
    // (see structure_block), because whether an indent is structural is a
    // question about the block, not about the line. Everything the commands
    // below append to `doc.detail` directly has to flush this buffer first, or
    // it would land ahead of prose the author wrote before it.
    std::vector<std::string> detail_lines;
    auto                     flush_detail = [&] {
        append_paragraph(doc.detail, structure_block(detail_lines, spans));
        detail_lines.clear();
    };

    auto append_to_sink = [&](std::string const &text) {
        std::string const t = convert_inline(text);
        switch (sink) {
        case Sink::Brief:
            if (!doc.brief.empty()) {
                doc.brief += " ";
            }
            doc.brief = trim(doc.brief + t);
            break;
        case Sink::Param:
            if (!doc.params.empty()) {
                doc.params.back().description = trim(doc.params.back().description + " " + t);
            }
            break;
        case Sink::TParam:
            if (!doc.tparams.empty()) {
                doc.tparams.back().description = trim(doc.tparams.back().description + " " + t);
            }
            break;
        case Sink::Return:
            if (!doc.returns.empty()) {
                doc.returns += " ";
            }
            doc.returns = trim(doc.returns + t);
            break;
        case Sink::Throw:
            if (!doc.throws_.empty()) {
                doc.throws_.back().description = trim(doc.throws_.back().description + " " + t);
            }
            break;
        case Sink::Deprecated:
            doc.deprecated_note = trim(doc.deprecated_note + " " + t);
            break;
        case Sink::Detail:
            detail_lines.push_back(t);
            break;
        }
    };

    for (std::string const &line : lines) {
        std::string const stripped = ltrim(line);

        // A blank line ends a structured field and a directive body. It is
        // kept in the detail buffer rather than acted on here, because what a
        // blank line means to the block below it - the list continues, the
        // item gets a second paragraph, the paragraph ends - is structure_block's
        // question to answer.
        if (is_blank(line)) {
            if (pending.active) {
                flush_pending(doc.detail, pending, spans);
            } else if (sink == Sink::Detail) {
                detail_lines.emplace_back();
            }
            sink = Sink::Detail;
            continue;
        }

        std::string cmd;
        std::string brace_arg;
        std::string rest;
        // An inline command opening a line is prose, not a block: fall through
        // to the continuation path so it joins its bullet item and reaches
        // convert_inline.
        if (match_command(stripped, cmd, brace_arg, rest) && !is_inline_command(cmd)) {
            // A new command ends the prose block and any pending directive
            // body, in that order: both append to detail, and the prose came
            // first.
            flush_detail();
            flush_pending(doc.detail, pending, spans);

            if (cmd == "brief") {
                sink      = Sink::Brief;
                saw_brief = true;
                append_to_sink(rest);
            } else if (cmd == "param") {
                doc.params.push_back(parse_named(rest));
                doc.params.back().description = convert_inline(doc.params.back().description);
                sink                          = Sink::Param;
            } else if (cmd == "tparam") {
                doc.tparams.push_back(parse_named(rest));
                doc.tparams.back().description = convert_inline(doc.tparams.back().description);
                sink                           = Sink::TParam;
            } else if (cmd == "return" || cmd == "returns" || cmd == "result") {
                sink = Sink::Return;
                append_to_sink(rest);
            } else if (cmd == "throws" || cmd == "throw" || cmd == "exception") {
                doc.throws_.push_back(parse_named(rest));
                doc.throws_.back().description = convert_inline(doc.throws_.back().description);
                sink                           = Sink::Throw;
            } else if (cmd == "versionadded" || cmd == "versionchanged" || cmd == "versionremoved") {
                // Single-line version note (alias form @versionadded{x}).
                std::string note = ".. ";
                note += cmd;
                note += ":: ";
                note += brace_arg;
                append_paragraph(doc.detail, note);
                sink = Sink::Detail;
            } else if (cmd == "since") {
                // Structured availability — captured as a field (and rendered
                // as a ``.. versionadded::`` badge), not folded into detail.
                doc.since = trim(!brace_arg.empty() ? brace_arg : rest);
                sink      = Sink::Detail;
            } else if (cmd == "deprecated") {
                doc.deprecated      = true;
                doc.deprecated_note = trim(!brace_arg.empty() ? brace_arg : rest);
                sink                = Sink::Deprecated; // continuation lines extend the note
            } else if (cmd == "versionaddeddesc" || cmd == "versionchangeddesc" || cmd == "versionremoveddesc") {
                pending.active    = true;
                pending.directive = cmd.substr(0, cmd.size() - 4); // strip "desc"
                pending.arg       = brace_arg;
                sink              = Sink::Detail;
            } else if (cmd == "endversion" || cmd == "endrst" || cmd == "endcode" || cmd == "endverbatim") {
                // Terminators: pending already flushed above; nothing to add.
                sink = Sink::Detail;
            } else if (!admonition_for(cmd).empty()) {
                pending.active    = true;
                pending.directive = admonition_for(cmd);
                pending.arg.clear();
                if (pending.directive == "admonition") {
                    pending.arg = (cmd == "post") ? "Postcondition" : "Precondition";
                }
                if (!rest.empty()) {
                    pending.body.push_back(convert_inline(rest));
                }
                sink = Sink::Detail;
            } else if (cmd == "par") {
                pending.active    = true;
                pending.directive = "par";
                pending.arg       = convert_inline(rest);
                sink              = Sink::Detail;
            } else if (cmd == "copydoc" || cmd == "copybrief" || cmd == "copydetails") {
                append_paragraph(doc.detail, "See ``" + rest + "``.");
                sink = Sink::Detail;
            } else if (drop_commands().count(cmd) != 0) {
                // Entity-kind / grouping marker: drop the command. If it
                // carried trailing prose (rare), keep that as detail.
                if (cmd == "details" && !rest.empty()) {
                    sink = Sink::Detail;
                    append_to_sink(rest);
                }
                // otherwise: line dropped entirely
            } else {
                // Unknown command: keep its text (minus the marker) so
                // nothing silently vanishes.
                append_to_sink(rest);
            }
            continue;
        }

        // Plain prose.
        //
        // Prose that reaches a block goes in WITH its indentation, which
        // DocExtractor preserved for exactly this: whether that indentation is
        // a nesting level or a wrapped line is decided later, against the
        // block around it. Every other sink is a field body, where a wrap is
        // all an indent can be, so those keep taking the line ltrimmed.
        if (pending.active) {
            pending.body.push_back(convert_inline(rtrim(line)));
        } else if (sink == Sink::Detail) {
            detail_lines.push_back(convert_inline(rtrim(line)));
        } else {
            append_to_sink(stripped);
        }
    }
    flush_detail();
    flush_pending(doc.detail, pending, spans);

    // No explicit @brief: promote the first detail paragraph to the brief.
    if (!saw_brief && doc.brief.empty() && !doc.detail.empty()) {
        std::size_t const para = doc.detail.find("\n\n");
        if (para == std::string::npos) {
            doc.brief = doc.detail;
            doc.detail.clear();
        } else {
            doc.brief  = doc.detail.substr(0, para);
            doc.detail = doc.detail.substr(para + 2);
        }
    }

    // Splice protected spans back in, then tidy whitespace.
    doc.brief   = trim(restore_spans(doc.brief, spans));
    doc.detail  = trim(collapse_blank_lines(restore_spans(doc.detail, spans)));
    doc.returns = trim(restore_spans(doc.returns, spans));
    for (auto &p : doc.params) {
        p.description = trim(restore_spans(p.description, spans));
    }
    for (auto &p : doc.tparams) {
        p.description = trim(restore_spans(p.description, spans));
    }
    for (auto &t : doc.throws_) {
        t.description = trim(restore_spans(t.description, spans));
    }
    return doc;
}

namespace {

// One numpydoc section: a header, a line of dashes as long as the header, then
// the body. Appended only when the body has something in it.
void append_section(std::string &out, std::string const &header, std::string const &body) {
    if (trim(body).empty()) {
        return;
    }
    if (!out.empty()) {
        out += "\n\n";
    }
    out += header + "\n" + std::string(header.size(), '-') + "\n" + rtrim(body);
}

// A numpydoc named entry: the name on its own line, the description indented
// under it. The type slot ("name : type") is left off on purpose - the C++ type
// is not the Python type, pybind11 already prepends a typed signature line, and
// the stub carries the real annotation.
std::string named_entry(std::string const &name, std::string const &description) {
    std::string out = name.empty() ? std::string{"?"} : name;
    std::string const desc = trim(description);
    if (desc.empty()) {
        return out + "\n";
    }
    out += "\n";
    // Indent every line by four, including the wrapped continuations of a
    // description that already spans lines.
    std::size_t start = 0;
    for (std::size_t i = 0; i <= desc.size(); ++i) {
        if (i == desc.size() || desc[i] == '\n') {
            std::string const line = desc.substr(start, i - start);
            out += line.empty() ? std::string{"\n"} : "    " + line + "\n";
            start = i + 1;
        }
    }
    return out;
}

} // namespace

std::string render_python_docstring(DocComment const &doc) {
    std::string out;

    if (!doc.brief.empty()) {
        out += doc.brief;
    }
    if (!doc.detail.empty()) {
        if (!out.empty()) {
            out += "\n\n";
        }
        out += doc.detail;
    }

    std::string params;
    for (auto const &p : doc.params) {
        params += named_entry(p.name, p.description);
    }
    append_section(out, "Parameters", params);

    append_section(out, "Returns", trim(doc.returns));

    std::string raises;
    for (auto const &t : doc.throws_) {
        raises += named_entry(t.name, t.description);
    }
    append_section(out, "Raises", raises);

    // "Warnings" is numpydoc's admonition section, and a deprecation is the
    // thing a reader most needs to see. Doxygen's @deprecated carries no
    // version, so this cannot be a ``.. deprecated::`` directive (Sphinx
    // requires the version argument) - plain prose says the same thing and
    // reads correctly in a terminal.
    if (doc.deprecated) {
        std::string note = trim(doc.deprecated_note);
        append_section(out, "Warnings", note.empty() ? std::string{"Deprecated."} : "Deprecated. " + note);
    }

    if (!doc.since.empty()) {
        append_section(out, "Notes", ".. versionadded:: " + trim(doc.since));
    }

    return trim(out);
}

std::string python_docstring(std::string const &raw) {
    if (trim(raw).empty()) {
        return {};
    }
    return render_python_docstring(parse_doc_comment(raw));
}

} // namespace apiary
