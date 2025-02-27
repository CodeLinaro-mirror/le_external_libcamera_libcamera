/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2024, Amlogic
 *
 * C3ISP AGC/AEC mean-based control algorithm
 */

#pragma once

#include <linux/c3-isp-config.h>

#include <libcamera/base/span.h>
#include <libcamera/base/utils.h>

#include <libcamera/geometry.h>

#include "libipa/agc_mean_luminance.h"
#include "algorithm.h"

namespace libcamera {

namespace ipa::c3isp::algorithms {

class Agc : public Algorithm, public AgcMeanLuminance
{
public:
	Agc();
	~Agc() = default;

	int init(IPAContext &context, const YamlObject &tuningData) override;
	int configure(IPAContext &context, const IPACameraSensorInfo &configInfo) override;
	void queueRequest(IPAContext &context, const uint32_t frame,
			  IPAFrameContext &frameContext,
			  const ControlList &controls) override;
	void prepare(IPAContext &context, const uint32_t frame,
		     IPAFrameContext &frameContext,
		     C3ISPParams *params) override;
	void process(IPAContext &context, const uint32_t frame,
		     IPAFrameContext &frameContext,
		     const c3_isp_stats_info *stats,
		     ControlList &metadata) override;
private:
	Histogram parseStatistics(const c3_isp_stats_info *stats);
	double estimateLuminance(double gain) const override;

	std::vector<uint8_t> lumaMeans_;
};

} /* namespace ipa::c3isp::algorithms */

} /* namespace libcamera */
