//----------------------------------------------------------------------------------------------
// Copyright (c) The Einsums Developers. All rights reserved.
// Licensed under the MIT License. See LICENSE.txt in the project root for license information.
//----------------------------------------------------------------------------------------------

// Docstring fixture: the whole surface a Python docstring is composed from.
//
// This is the regression pin for Finding 11. The binding and stub emitters used
// to hand the RAW Doxygen comment straight to pybind11 and to the .pyi, so
// `help()` on a shipped Einsums function opened with "@brief Set the seed of
// the random number generator." Both now render numpydoc from the parsed
// structure, and both must render the SAME text - a reader hovering in an
// editor and a reader calling help() are looking at one function.
//
// numpydoc rather than any other convention because it is already this
// project's Python docstring grammar: apiary_py_extract.py parses numpydoc out
// of .py sources into the very same structure this renders from, so a
// C++-origin binding and a Python-origin function read alike.
//
// Every entity below exists to put one section, or one absence, in the golden.

#pragma once

#include <apiary/Annotations.hpp>

namespace einsums::fixture {

/**
 * @brief Every section at once.
 *
 * A longer explanation that wraps across
 * two source lines, to prove the extended summary survives the wrap.
 *
 * @param value The input value, with a description long enough that it wraps
 *              onto a second source line and must be re-indented under its
 *              name rather than left dangling.
 * @param factor The scale factor.
 * @tparam T A template parameter, which must NOT appear: it is pinned by the
 *           instantiation and a Python caller has nothing to pass.
 * @return The scaled result, described across
 *         two lines as well.
 * @throws std::invalid_argument When @p factor is zero.
 * @since 1.2.0
 * @deprecated Use ``rescale`` instead.
 */
APIARY_EXPOSE double every_section(double value, double factor);

/// Brief only, with nothing else. The docstring must be exactly this line -
/// no empty sections, no trailing blank.
APIARY_EXPOSE double brief_only(double x);

/**
 * @brief A parameter documented without a description.
 *
 * numpydoc allows a bare name, and an entry that renders as a name with an
 * empty indented block underneath is malformed.
 *
 * @param x
 * @param y The second one, which does have a description.
 */
APIARY_EXPOSE double sparse_params(double x, double y);

/**
 * @brief Deprecated with no migration note.
 *
 * The Warnings section still has to say something; an empty admonition is
 * worse than none.
 *
 * @deprecated
 */
APIARY_EXPOSE double bare_deprecated(double x);

/**
 * @brief Prose that is itself reST.
 *
 * Doc comments may contain reST by design, and a docstring is read as reST by
 * every tool that renders one, so blocks must survive into it intact.
 *
 * @code{.py}
 * scaled = every_section(2.0, 3.0)
 * @endcode
 *
 * @note An admonition, which becomes a directive in the docstring too.
 *
 * @param x The value.
 */
APIARY_EXPOSE double rest_in_prose(double x);

/**
 * @brief Math, which is LaTeX, which is backslashes.
 *
 * A docstring is a Python string literal, so `\sum` in it is an escape
 * sequence Python does not know. Since 3.12 that is a SyntaxWarning that says
 * it "will not work in the future", which puts the stub on a path to not
 * parsing at all. The literal has to be raw.
 *
 * The contraction is @f$ c_{ik} = \sum_j a_{ij} b_{jk} @f$, and as a block:
 *
 * @f[
 *   \alpha \sum_k \mathrm{tr}(A_k)
 * @f]
 *
 * @param x The value.
 */
APIARY_EXPOSE double with_math(double x);

/// @ingroup internal_group
APIARY_EXPOSE double only_a_dropped_command(double x);

} // namespace einsums::fixture
