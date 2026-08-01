//----------------------------------------------------------------------------------------------
// Copyright (c) The Einsums Developers. All rights reserved.
// Licensed under the MIT License. See LICENSE.txt in the project root for license information.
//----------------------------------------------------------------------------------------------

#include "DocComment.hpp"

#include <cctype>
#include <cstdint>
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

// Append one prose line to the current paragraph in `detail` (wrapped source
// lines flow together; reST collapses the single newline to a space).
void append_detail_line(std::string &detail, std::string const &line) {
    if (!detail.empty() && detail.back() != '\n') {
        detail += "\n";
    }
    detail += line;
}

// A line that, after trimming, begins a reST bullet item: "- ", "+ ", or
// "* " (but not "**bold").
bool is_bullet_item(std::string const &text) {
    std::string const s = ltrim(text);
    if (s.size() >= 2 && (s[0] == '-' || s[0] == '+' || s[0] == '*') && s[1] == ' ') {
        return true;
    }
    // Enumerated list item: digits followed by ``.``/``)`` and a space
    // (``13. Foo``). Like bullets, their wrapped continuation lines must be
    // joined or reST reports "enumerated list ends without a blank line".
    std::size_t i = 0;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
        ++i;
    }
    return i > 0 && i + 1 < s.size() && (s[i] == '.' || s[i] == ')') && s[i + 1] == ' ';
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

// Join wrapped continuation lines of bullet / enumerated items. Directive
// bodies (``@par`` / ``@note`` …) accumulate raw lines that were ltrimmed, so a
// wrapped item ("- foo\n  bar") would dedent and break the list in reST. This
// mirrors the bullet handling on the Detail sink.
std::string join_bullet_continuations(std::string const &text) {
    std::vector<std::string> out;
    bool                     in_bullet = false;
    std::size_t              start     = 0;
    for (std::size_t i = 0; i <= text.size(); ++i) {
        if (i != text.size() && text[i] != '\n') {
            continue;
        }
        std::string const line = text.substr(start, i - start);
        start                  = i + 1;
        if (is_bullet_item(line)) {
            in_bullet = true;
            out.push_back(line);
        } else if (in_bullet && !trim(line).empty()) {
            out.back() += " " + trim(line);
        } else {
            in_bullet = false;
            out.push_back(line);
        }
    }
    std::string res;
    for (auto const &l : out) {
        if (!res.empty()) {
            res += "\n";
        }
        res += l;
    }
    return res;
}

void flush_pending(std::string &detail, Pending &p) {
    if (!p.active) {
        return;
    }
    std::string body;
    for (auto const &l : p.body) {
        if (!body.empty()) {
            body += "\n";
        }
        body += l;
    }
    body = join_bullet_continuations(trim(body));

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
    std::string const          protectedText = protect_spans(cleaned, spans);

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
    bool    in_bullet = false; // true while accumulating a bullet-list item in detail

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
            if (is_bullet_item(t)) {
                in_bullet = true;
                append_detail_line(doc.detail, t);
            } else if (in_bullet && !trim(t).empty()) {
                // Continuation of the current bullet item. The line arrives
                // ltrimmed (see the loop below), which would dedent out of the
                // list; join onto the item with a space so it stays one logical
                // line whatever the author's continuation indent was.
                if (!doc.detail.empty() && doc.detail.back() != ' ' && doc.detail.back() != '\n') {
                    doc.detail += " ";
                }
                doc.detail += t;
            } else {
                append_detail_line(doc.detail, t);
            }
            break;
        }
    };

    for (std::string const &line : lines) {
        std::string const stripped = ltrim(line);

        // Placeholder-only line (a protected block) → flush state, drop into detail.
        if (is_blank(line)) {
            flush_pending(doc.detail, pending);
            in_bullet = false; // a blank line ends a bullet list
            // A blank line ends a structured field and forces a paragraph
            // break: ensure detail ends with exactly one blank line so the
            // next content starts a fresh block (critical after a bullet
            // list, which reST requires be followed by a blank line).
            if (sink == Sink::Detail && !doc.detail.empty()) {
                while (!doc.detail.empty() && doc.detail.back() == '\n') {
                    doc.detail.pop_back();
                }
                doc.detail += "\n\n";
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
            // A new command always terminates a pending directive body.
            flush_pending(doc.detail, pending);
            in_bullet = false;

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

        // Plain continuation line.
        //
        // Prose lines are ltrimmed, even though DocExtractor now preserves what
        // the author wrote. That looks like throwing away the fix, and is not:
        // indentation on a prose line is almost always COSMETIC - a wrapped
        // sentence, a `:math:` role continued on the next line, an enumerated
        // list whose numbers are right-aligned so `2.` sits one column deeper
        // than `10.`. reST reads every one of those structurally and reports a
        // block quote that ends without a blank line. Preserving it turned six
        // such comments in Einsums into docs-build failures.
        //
        // Telling cosmetic indentation from structural (a nested bullet, a
        // definition list) needs real block-structure parsing, which is
        // Finding 4 and a bigger change than this. What the preserved text DOES
        // buy is the protected spans: @rst and @code bodies are extracted from
        // it before this loop runs, so a directive spliced through @rst keeps
        // the indentation that makes its body a body.
        if (pending.active) {
            pending.body.push_back(convert_inline(stripped));
        } else {
            append_to_sink(stripped);
        }
    }
    flush_pending(doc.detail, pending);

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
