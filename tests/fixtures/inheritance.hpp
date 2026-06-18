//----------------------------------------------------------------------------------------------
// Copyright (c) The Einsums Developers. All rights reserved.
// Licensed under the MIT License. See LICENSE.txt in the project root for license information.
//----------------------------------------------------------------------------------------------

// Phase-1 (docs-graph) fixture: a base class and a derived class that overrides
// a virtual method. Exercises the symbol_id (USR) capture plus the
// inheritsFrom / overrides / memberOf relationship edges.

#pragma once

#include <apiary/Annotations.hpp>

namespace einsums::fixture {

/// A minimal tensor base.
class APIARY_EXPOSE Tensor {
  public:
    /// Default-construct an empty tensor.
    APIARY_EXPOSE Tensor();

    /// The Frobenius norm. Overridden by block-structured subclasses.
    APIARY_EXPOSE virtual double norm() const;

    /// Number of stored elements.
    APIARY_EXPOSE virtual int size() const;
};

/// A block-structured tensor.
class APIARY_EXPOSE BlockTensor : public Tensor {
  public:
    /// Default-construct an empty block tensor.
    APIARY_EXPOSE BlockTensor();

    /// Block-aware norm; overrides Tensor::norm.
    APIARY_EXPOSE double norm() const override;
};

} // namespace einsums::fixture
