/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2021-2022, Ideas On Board
 *
 * RkISP1 Lens Shading Correction control
 */

#pragma once

#include <vector>

#include <linux/rkisp1-config.h>

#include "libcamera/internal/value_node.h"
#include "libipa/fixedpoint.h"

#include "libipa/lsc.h"

#include "algorithm.h"

namespace libcamera {

namespace ipa {

namespace rkisp1::algorithms {

using RkISP1LscAlgorithm = LscAlgorithm<UQ<2, 10>>;
using RkISP1Components = RkISP1LscAlgorithm::Components;
using RkISP1ComponentsMap = RkISP1LscAlgorithm::ComponentsMap;

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
	void setParameters(rkisp1_cif_isp_lsc_config &config);
	void copyTable(rkisp1_cif_isp_lsc_config &config,
		       const RkISP1Components &set);

	std::vector<double> xSize_;
	std::vector<double> ySize_;
	std::vector<double> xPos_;
	std::vector<double> yPos_;
	uint16_t xGrad_[RKISP1_CIF_ISP_LSC_SECTORS_TBL_SIZE];
	uint16_t yGrad_[RKISP1_CIF_ISP_LSC_SECTORS_TBL_SIZE];
	uint16_t xSizes_[RKISP1_CIF_ISP_LSC_SECTORS_TBL_SIZE];
	uint16_t ySizes_[RKISP1_CIF_ISP_LSC_SECTORS_TBL_SIZE];

	unsigned int lastAppliedCt_;
	unsigned int lastAppliedQuantizedCt_;

	RkISP1LscAlgorithm lscAlgo_;
};

} /* namespace rkisp1::algorithms */

#ifndef __DOXYGEN__
template<typename T>
void interpolateVector(const std::vector<T> &a,
		       const std::vector<T> &b,
		       std::vector<T> &dest, double lambda)
{
	ASSERT(a.size() == b.size());
	dest.resize(a.size());
	for (size_t i = 0; i < a.size(); i++)
		dest[i] = a[i] * (1.0 - lambda) + b[i] * lambda;
}

template<>
void Interpolator<rkisp1::algorithms::RkISP1Components>::
	interpolate(const rkisp1::algorithms::RkISP1Components &a,
		    const rkisp1::algorithms::RkISP1Components &b,
		    rkisp1::algorithms::RkISP1Components &dest,
		    double lambda)
{
	for (auto const &[k, v] : a)
		interpolateVector(v, b.at(k), dest[k], lambda);
}
#endif /* __DOXYGEN__ */

} /* namespace ipa */

} /* namespace libcamera */
