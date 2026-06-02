//----------------------------------------------------------------------------------------------
// Copyright (c) The Einsums Developers. All rights reserved.
// Licensed under the MIT License. See LICENSE.txt in the project root for license information.
//----------------------------------------------------------------------------------------------

// Phase-2 fixture: scoped + unscoped enums, both at namespace scope and
// nested inside a class. Exercises BoundEnum + BoundEnumerator emission.

#pragma once

#include <apiary/Annotations.hpp>

namespace einsums::fixture {

enum class APIARY_EXPOSE Layout : int {
    /// Row-major contiguous storage.
    RowMajor = 0,
    /// Column-major contiguous storage.
    ColumnMajor = 1,
};

enum APIARY_EXPOSE Severity {
    Info = 0,
    Warning,
    Error,
};

class APIARY_EXPOSE Engine {
  public:
    enum class APIARY_EXPOSE State {
        Idle,
        Running,
        Stopped,
    };

    APIARY_EXPOSE State current_state() const;
};

} // namespace einsums::fixture
