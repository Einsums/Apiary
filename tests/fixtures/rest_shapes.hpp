//----------------------------------------------------------------------------------------------
// Copyright (c) The Einsums Developers. All rights reserved.
// Licensed under the MIT License. See LICENSE.txt in the project root for license information.
//----------------------------------------------------------------------------------------------

// Markup fixture: the reST *shapes* DocComment.cpp has to get right.
//
// Every other fixture in this directory pins what the emitter generates. This
// one pins what the doc converter generates, because that is the half of the
// pipeline with the least coverage and the most regressions. Each entity below
// is one shape, named for it, and the golden holds the converted text plus the
// doc_lint diagnostics - so a conversion that starts producing invalid reST is
// visible as a golden diff, at the shape that broke, instead of as a docutils
// warning about a generated file three build steps later.
//
// Doc comments here are written the way an author would write them. Several
// shapes come in pairs that are nearly identical on the page and must convert
// differently - a nested list against a right-aligned enumerator, a definition
// list against a hanging indent - because those pairs are where the converter
// has actually gone wrong. Where a shape is converted WRONG, the comment says
// so and names the finding; the golden records the broken output until the fix
// lands, at which point the golden diff is the proof.

#pragma once

#include <apiary/Annotations.hpp>

namespace einsums::fixture {

/**
 * @brief A bullet list whose items wrap across source lines.
 *
 * The wrapped continuation lines must be joined onto their item. reST requires
 * a bullet item's continuation to be indented to the item's text column, and
 * prose lines arrive ltrimmed, so anything other than joining produces
 * "Bullet list ends without a blank line".
 *
 * - The first item is long enough that it wraps onto a second source line and
 *   then onto a third, all of which belong to the same item.
 * - The second item, to prove the list continues.
 *
 * A trailing paragraph, which must be separated from the list by a blank line.
 */
APIARY_EXPOSE void wrapped_bullets();

/**
 * @brief A nested bullet list.
 *
 * Nesting IS indentation, so this is one of the two shapes that made the
 * converter carry block structure (Finding 4). An inner item clears its
 * parent's TEXT column, which is what tells it from the shapes below whose
 * indentation is cosmetic; the columns are then re-emitted canonically rather
 * than copied, so the author's exact spacing does not have to be right.
 *
 * - An outer item.
 *   - An inner item, indented under it.
 *   - A second inner item.
 * - A second outer item.
 */
APIARY_EXPOSE void nested_bullets();

/**
 * @brief A lead-in paragraph followed by an indented list.
 *
 * By far the commonest indentation-bearing shape in real headers, and the one
 * a naive fix gets wrong in the other direction: the author indents the list
 * under the sentence that introduces it, but means one list, not a list inside
 * a block quote. It is emitted flush, separated from the paragraph by the
 * blank line reST wants there. The lead-in wins:
 *   - The first item, indented under the sentence above.
 *   - The second item, which wraps onto a second source line to prove the
 *     continuation still joins at this level.
 */
APIARY_EXPOSE void lead_in_then_indented_list();

/**
 * @brief An enumerated list numbered right-aligned.
 *
 * The first counterexample that killed the naive fix. The numbers are aligned
 * on their last digit, so ``2.`` sits one column deeper than ``10.`` and the
 * indentation is pure typography. Preserved, reST reads the first nine items
 * as a block quote and reports "Block quote ends without a blank line" at the
 * tenth. An item that lands short of the enclosing item's text column but at
 * or past its marker is a sibling, so all of these come out at one level.
 *
 * The numbering runs consecutively on purpose: docutils ends an enumerated
 * list at the first number that does not follow its predecessor, so a fixture
 * that skipped from 2 to 9 would report a defect of its own making.
 *
 *  1. The first step.
 *  2. The second step.
 *  3. The third step.
 *  4. The fourth step.
 *  5. The fifth step.
 *  6. The sixth step.
 *  7. The seventh step.
 *  8. The eighth step.
 *  9. The ninth step.
 * 10. The tenth step, whose number is one character wider.
 * 11. The eleventh step.
 */
APIARY_EXPOSE void right_aligned_enumerators();

/**
 * @brief Prose wrapped with a hanging indent.
 *
 * The second counterexample. A sentence whose continuation is indented and
 * which then returns to the base indent is what reST reports as "Definition
 * list ends without a blank line", and it is the shape a wrapped ``@note`` or
 * ``@brief`` naturally takes. Only the indent is dropped here; the author's
 * line breaks stay, because a paragraph's lines flow together anyway:
 *
 * A sentence that wraps
 *     with a hanging indent
 * and then continues at the base indent.
 *
 * A single term with a single body is character-identical to a wrap whose
 * indented part happens to end the paragraph, and in real headers that wrap is
 * an order of magnitude commoner, so a lone pair is deliberately flattened:
 *
 * Fills the result with
 *     the sum over k of a_{ik} b_{kj}.
 */
APIARY_EXPOSE void hanging_indent_wrap();

/**
 * @brief A list item carrying a second paragraph.
 *
 * A blank line inside a list does not necessarily end it. What follows is part
 * of the item when it clears the item's text column, and ends the list when it
 * does not - which is the same column test the nesting decision uses.
 *
 * - An item whose first paragraph says one thing.
 *
 *   A second paragraph, still inside the item.
 * - A second item.
 *
 * A paragraph that ends the list, because it starts at column 0.
 */
APIARY_EXPOSE void item_with_second_paragraph();

/**
 * @brief An enumerated list whose items wrap.
 *
 * 1. The first step, written long enough that it wraps onto a second source
 *    line the way an author would naturally write it.
 * 2. The second step.
 * 3. The third step.
 */
APIARY_EXPOSE void enumerated_list();

/**
 * @brief A ``@par`` block whose body is a bullet list.
 *
 * @par Limitations
 * - A directive body is accumulated separately and joined by its own helper,
 *   so it is a second code path with the same wrapped-continuation problem.
 * - A second item, to close the list.
 */
APIARY_EXPOSE void par_with_bullets();

/**
 * @brief An inline command at the start of a continuation line.
 *
 * An author wrapping a sentence has no control over which token lands first on
 * a line. A line whose first token starts with a command marker used to be
 * taken for a block-level command, so the line below neither joined its bullet
 * nor passed through inline conversion: the item dedented to column 0 (which is
 * what Sphinx reported, about a generated file) and the reference lost its
 * markup. It must stay one joined item with a literal-formatted reference.
 *
 * - A bullet whose continuation line begins with an inline command, such as
 *   @ref einsums::fixture::wrapped_bullets, and then continues with prose that
 *   should stay joined to the same item.
 * - A second bullet, to close the list.
 */
APIARY_EXPOSE void inline_command_at_line_start();

/**
 * @brief Every inline command, each opening a continuation line.
 *
 * The rest of the closed set, in a plain paragraph rather than a list. Nothing
 * about the reST is invalid here, so the markup check cannot see it: the text
 * golden is the only thing that catches an inline command silently losing its
 * markup.
 *
 * A paragraph that wraps so that
 * @c std::complex<double> starts a line, and another that wraps so that
 * @p count starts one, and
 * @a emphasis, and
 * @b bold, and
 * @e also_emphasis all do too.
 */
APIARY_EXPOSE void inline_commands_wrapped();

/**
 * @brief A bullet list directly adjacent to a code block.
 *
 * The code deliberately contains characters reST reads as markup. A code block
 * is not reST and must never be checked as if it were, or every documented
 * example turns into an invented defect - `scale *= 2` alone would report an
 * unterminated emphasis.
 *
 * - An item before the code.
 * - A second item.
 *
 * @code{.py}
 * scale = 1
 * scale *= 2            # an asterisk, and a `backtick` below
 * result = wrapped_bullets()
 * @endcode
 *
 * - An item after the code, opening a second list.
 */
APIARY_EXPOSE void bullets_around_code();

/**
 * @brief Math spans inline and as a block, next to a list.
 *
 * The inline form @f$ a_{ij} b_{jk} @f$ sits in running prose, and a long one
 * wraps across source lines the way an author writes it - which reST cannot
 * carry inside an inline role, so its interior whitespace has to be flattened:
 * @f$\sum_k \alpha_k\,\mathrm{einsum}(\text{spec}_k, A, B)
 *     = \mathrm{einsum}(\text{spec}_0, A,\; \sum_k \alpha_k\,P_k(B))@f$.
 *
 * The block form stands alone, where the layout IS the content:
 *
 * @f[
 *   C_{ik} = \sum_j a_{ij} b_{jk}
 * @f]
 *
 * - A list immediately after the math block.
 * - A second item.
 */
APIARY_EXPOSE void math_next_to_list();

/**
 * @brief Verbatim reST spliced through ``@rst``.
 *
 * Two things at once, and both were broken (Finding 5). Naming the span
 * command in the brief - inside an inline literal, as prose about it has to -
 * used to open a REAL span, because span protection scanned raw text; that
 * opener paired with the genuine terminator below and swallowed the comment.
 * And the spliced body arrived dedented, so the directive below had no content.
 *
 * A protected span is extracted from the raw comment BEFORE any line handling
 * runs, so what is spliced here is exactly what the author wrote, untouched by
 * the block structuring that ordinary prose goes through.
 *
 * @rst
 * .. note::
 *
 *    An authored directive, indented body and all.
 *
 * - A list inside the spliced block.
 * - A second item.
 * @endrst
 */
APIARY_EXPOSE void embedded_rst();

/**
 * @brief A definition list.
 *
 * The other half of Finding 4: a definition list is nothing BUT its
 * indentation. What distinguishes it from the hanging-indent wrap above is
 * that every term has a body AND that the alternation repeats - nobody wraps a
 * sentence by indenting, dedenting and indenting again.
 *
 * term one
 *     The definition of the first term, which wraps onto a second line.
 * term two
 *     The definition of the second term.
 */
APIARY_EXPOSE void definition_list();

/**
 * @brief Admonition bodies that contain lists.
 *
 * @note
 * - A bullet inside a note, whose continuation wraps onto a second source
 *   line and must stay joined.
 * - A second bullet.
 *
 * @warning A one-line warning.
 *
 * @see einsums::fixture::wrapped_bullets
 */
APIARY_EXPOSE void admonitions_with_lists();

/**
 * @brief Field descriptions carrying their own markup.
 *
 * @param values The input values. Long enough to wrap onto a second source
 *               line, which reST field bodies also require be joined.
 * @param scale A scale factor. See @ref einsums::fixture::wrapped_bullets for
 *              the reference form.
 * @tparam T The element type, as in @c std::complex<double>.
 * @return The accumulated result, described across two source lines so the
 *         wrap is exercised here too.
 * @throws std::invalid_argument When @p values is empty.
 */
template <typename T>
APIARY_EXPOSE APIARY_INSTANTIATE(double) T accumulate(T const *values, T scale = T{1});

/**
 * @brief Literal and emphasis spans that reST is fussy about.
 *
 * A template argument list after a literal, @c std::vector<int>, and a
 * subscript, @p extents[k], and a call, @a compute(), each of which must be
 * absorbed into the span so the closing delimiter is not followed by a
 * character reST reads as a dangling start-string.
 */
APIARY_EXPOSE void fussy_inline_spans();

/**
 * @brief An inline literal wrapped across a line that then starts with ``*``.
 *
 * Found in a real header, by the docs build, after block structure started
 * setting lists off from the paragraph above them. The author wrapped a
 * formula inside an inline literal, and the continuation happened to begin
 * with a multiplication sign followed by a space - which is also a bullet
 * marker. Read as a list, the literal is cut in half and reST reports an
 * inline literal start-string without an end-string.
 *
 * A line inside an unterminated literal is literal text, so no list starts
 * here and the two lines stay one paragraph:
 *
 * Computes ``C = c_pf * C + a_pf
 * * permute(A)`` according to the spec.
 */
APIARY_EXPOSE void literal_wrapped_onto_a_star();

/// An enum carries doc text too, and it is rendered as reST like any other.
///
/// - A bullet in an enum's detail, wrapping onto a second source line to
///   exercise the same join.
/// - A second bullet.
enum class APIARY_EXPOSE Shape {
    Round, ///< A round shape.
    Square ///< A square shape.
};

} // namespace einsums::fixture
