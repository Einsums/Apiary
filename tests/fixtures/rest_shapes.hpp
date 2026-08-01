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
// Doc comments here are written the way an author would write them. Where a
// shape is currently converted WRONG, the comment says so and names the
// finding; the golden records the broken output until the fix lands, at which
// point the golden diff is the proof.

#pragma once

#include <apiary/Annotations.hpp>

namespace einsums::fixture {

/**
 * @brief A bullet list whose items wrap across source lines.
 *
 * The wrapped continuation lines must be joined onto their item. reST requires
 * a bullet item's continuation to be indented to the item's text column, and
 * the comment normalizer has already stripped that indentation, so anything
 * other than joining produces "Bullet list ends without a blank line".
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
 * KNOWN BAD (Finding 4): the converter is line-oriented and ltrims every line,
 * so the indentation that expresses nesting is gone before anything looks at
 * it. The result is valid reST and a flat four-item list - which is why only
 * the text golden catches this one, not the markup check.
 *
 * - An outer item.
 *   - An inner item, indented under it.
 *   - A second inner item.
 * - A second outer item.
 */
APIARY_EXPOSE void nested_bullets();

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
 * KNOWN BAD (apiary_robustness_plan.md, Finding 1): a line whose first token
 * starts with ``@`` is taken for a block-level command, so the line below
 * neither joins its bullet nor passes through inline conversion. The item
 * dedents to column 0 and the ``@ref`` loses its markup.
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
 * KNOWN BAD (Finding 1): the same defect for the rest of the closed set.
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
 * - An item before the code.
 * - A second item.
 *
 * @code{.py}
 * result = wrapped_bullets()
 * @endcode
 *
 * - An item after the code, opening a second list.
 */
APIARY_EXPOSE void bullets_around_code();

/**
 * @brief Math spans inline and as a block, next to a list.
 *
 * The inline form @f$ a_{ij} b_{jk} @f$ sits in running prose, and the block
 * form stands alone:
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
 * @brief Verbatim reST spliced through an rst span.
 *
 * KNOWN BAD (Finding 5): naming the span command in the brief above - writing
 * it inside an inline literal, as prose about it has to - opens a real span.
 * Span protection scans the raw text and does not know that a backtick-quoted
 * command is not a command, so the opener inside the literal pairs with the
 * genuine terminator below and swallows the whole comment.
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
 * KNOWN BAD (Finding 4, same root cause as nested_bullets): a definition list
 * IS its indentation, so ltrimming every line flattens it into one paragraph.
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
