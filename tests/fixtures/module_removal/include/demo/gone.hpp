//----------------------------------------------------------------------------------------------
// Copyright (c) The Einsums Developers. All rights reserved.
// Licensed under the MIT License. See LICENSE.txt in the project root for license information.
//----------------------------------------------------------------------------------------------

#pragma once

#include <apiary/Annotations.hpp>

namespace demo {

/// A class whose module is removed from the second build.
class APIARY_EXPOSE Gone {
  public:
    APIARY_EXPOSE Gone() = default;

    /// Return one.
    APIARY_EXPOSE int value() const { return 1; }
};

} // namespace demo
