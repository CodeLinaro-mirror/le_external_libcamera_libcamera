/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2021-2022, Ideas On Board
 *
 * AWB control algorithm
 */

#pragma once

#include <optional>

#include "libipa/matrix_interpolator.h"

#include "algorithm.h"

namespace libcamera {

namespace ipa::rkisp1::algorithms {

class Awb : public Algorithm
{
public:
	Awb();
	~Awb() = default;

	int init(IPAContext &context, const YamlObject &tuningData) override;
	int configure(IPAContext &context, const IPACameraSensorInfo &configInfo) override;
	void queueRequest(IPAContext &context, const uint32_t frame,
			  IPAFrameContext &frameContext,
			  const ControlList &controls) override;
	void prepare(IPAContext &context, const uint32_t frame,
		     IPAFrameContext &frameContext,
		     rkisp1_params_cfg *params) override;
	void process(IPAContext &context, const uint32_t frame,
		     IPAFrameContext &frameContext,
		     const rkisp1_stat_buffer *stats,
		     ControlList &metadata) override;

private:
	uint32_t estimateCCT(double red, double green, double blue);

	std::optional<MatrixInterpolator<double, 2, 1>> gains_;
	bool rgbMode_;
};

} /* namespace ipa::rkisp1::algorithms */
} /* namespace libcamera */
