/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2025, Ideas On Board
 *
 * RkISP1 Denoising Algorithms Base Class
 */

#pragma once

#include "algorithm.h"

namespace libcamera {

namespace ipa::rkisp1::algorithms {

/**
 * \class DenoiseBaseAlgorithm
 * \brief Base class for RkISP1 denoising algorithms
 *
 * This abstract base class provides common functionality for denoising algorithms
 * in the RkISP1 Image Processing Algorithm (IPA).
 *
 * Derived classes must implement algorithm-specific behavior.
 */
class DenoiseBaseAlgorithm : public ipa::rkisp1::Algorithm
{
protected:
	DenoiseBaseAlgorithm() = default;
	~DenoiseBaseAlgorithm() = default;
};

} /* namespace ipa::rkisp1::algorithms */

} /* namespace libcamera */
