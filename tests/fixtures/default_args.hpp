//----------------------------------------------------------------------------------------------
// Copyright (c) The Einsums Developers. All rights reserved.
// Licensed under the MIT License. See LICENSE.txt in the project root for license information.
//----------------------------------------------------------------------------------------------

// Default-argument fixture: defaults that are not literals.
//
// A default argument has to survive two different translations. The emitter
// re-emits it as C++ inside a py::arg(...) = ..., where it must still name
// something visible at that point in the generated TU; the stub emitter
// re-renders it as a Python expression, where it must be a value pyright
// accepts. Literals make both trivial and hide every interesting case.
//
// The nested-enum default is the one with history: it forces the emitter to
// order the enum's binding before the function that references it, and getting
// that ordering wrong produced a generated TU that named an enum which did not
// exist yet.

#pragma once

#include <apiary/Annotations.hpp>

namespace einsums::fixture {

/// Owns a nested enum used as a default argument below.
class APIARY_EXPOSE Solver {
  public:
    /// How aggressively to converge.
    enum class APIARY_EXPOSE Mode {
        Fast,   ///< Stop early.
        Exact   ///< Run to convergence.
    };

    APIARY_EXPOSE Solver();

    /// Default argument naming a nested enumerator: the enum's binding must be
    /// emitted before this method's.
    APIARY_EXPOSE void run(Mode mode = Mode::Fast);
};

/// A scoped enum at namespace scope, for the non-nested comparison.
enum class APIARY_EXPOSE Layout {
    RowMajor, ///< C order.
    ColMajor  ///< Fortran order.
};

/// Default argument naming a namespace-scope enumerator.
APIARY_EXPOSE void arrange(Layout layout = Layout::ColMajor);

template <typename T>
struct Point {
    T x;
    T y;
};

/// Default argument that is a template instantiation with a braced initializer.
/// Both halves have to survive: the C++ spelling needs the full template-id,
/// and the Python rendering has no way to spell it at all.
template <typename T>
APIARY_EXPOSE APIARY_INSTANTIATE_AS("translate", double) void translate(Point<T> &p, Point<T> delta = Point<T>{});

/// Defaults that are expressions rather than values: an arithmetic constant
/// fold and a negative number, both of which a naive re-render can mangle.
APIARY_EXPOSE double taper(double x, double falloff = 1.0 / 3.0, int offset = -1);

/// A default-constructed class type, and a bool/char pair - the two literal
/// kinds whose Python spelling differs from their C++ spelling.
APIARY_EXPOSE void configure(bool verbose = false, char tag = 'x');

} // namespace einsums::fixture
