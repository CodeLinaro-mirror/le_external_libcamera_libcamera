/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2021-2022, Ideas On Board
 *
 * RkISP1 Lens Shading Correction algorithm
 */

#pragma once

#include <vector>

#include <linux/rkisp1-config.h>

#include "libcamera/internal/value_node.h"

#include "libipa/fixedpoint.h"
#include "libipa/lsc.h"

#include "algorithm.h"
#include "ipa_context.h"
#include "params.h"

namespace libcamera {

namespace ipa::rkisp1::algorithms {

class LensShadingCorrection : public Algorithm
{
public:
	LensShadingCorrection();
	~LensShadingCorrection() = default;

	int init(IPAContext &context, const ValueNode &tuningData) override;
	int configure(IPAContext &context, const IPACameraSensorInfo &configInfo) override;
	void queueRequest(IPAContext &context, const uint32_t frame,
			  IPAFrameContext &frameContext,
			  const ControlList &controls) override;
	void prepare(IPAContext &context, const uint32_t frame,
		     IPAFrameContext &frameContext,
		     RkISP1Params *params) override;
	void process(IPAContext &context, const uint32_t frame,
		     IPAFrameContext &frameContext,
		     const rkisp1_stat_buffer *stats,
		     ControlList &metadata) override;

private:
	std::vector<double> parseSizes(const ValueNode &tuningData,
				       const char *prop);
	std::vector<double> sizesToPositions(Span<const double> sizes);

	void setParameters(rkisp1_cif_isp_lsc_config &config);
	void copyTable(rkisp1_cif_isp_lsc_config &config,
		       const ipa::lsc::Components<uint16_t> &set0);

	std::vector<double> xSize_;
	std::vector<double> ySize_;
	uint16_t xGrad_[RKISP1_CIF_ISP_LSC_SECTORS_TBL_SIZE];
	uint16_t yGrad_[RKISP1_CIF_ISP_LSC_SECTORS_TBL_SIZE];
	uint16_t xSizes_[RKISP1_CIF_ISP_LSC_SECTORS_TBL_SIZE];
	uint16_t ySizes_[RKISP1_CIF_ISP_LSC_SECTORS_TBL_SIZE];
	std::vector<double> xPos_;
	std::vector<double> yPos_;

	unsigned int lastAppliedCt_;
	unsigned int lastAppliedQuantizedCt_;

	LscAlgorithm<uint16_t, UQ<2, 10>> lscAlgo_;
};

} /* namespace ipa::rkisp1::algorithms */
} /* namespace libcamera */
