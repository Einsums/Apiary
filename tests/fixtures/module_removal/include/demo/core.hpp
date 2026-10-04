//----------------------------------------------------------------------------------------------
// Copyright (c) The Einsums Developers. All rights reserved.
// Licensed under the MIT License. See LICENSE.txt in the project root for license information.
//----------------------------------------------------------------------------------------------

#pragma once

#include <apiary/Annotations.hpp>

namespace demo {

/// A class whose module stays in every build.
class APIARY_EXPOSE Kept {
  public:
    APIARY_EXPOSE Kept() = default;

    /// Return one.
    APIARY_EXPOSE int value() const { return 1; }
};

} // namespace demo
