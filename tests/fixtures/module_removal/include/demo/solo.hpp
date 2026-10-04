//----------------------------------------------------------------------------------------------
// Copyright (c) The Einsums Developers. All rights reserved.
// Licensed under the MIT License. See LICENSE.txt in the project root for license information.
//----------------------------------------------------------------------------------------------

#pragma once

#include <apiary/Annotations.hpp>

namespace demo {

/// The only class in the ``solo`` submodule; its module is removed from the second build.
class APIARY_EXPOSE APIARY_MODULE("solo") Solo {
  public:
    APIARY_EXPOSE Solo() = default;

    /// Return one.
    APIARY_EXPOSE int value() const { return 1; }
};

} // namespace demo
