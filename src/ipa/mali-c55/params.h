/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2025, Ideas On Board
 *
 * Mali C55 ISP Parameters
 */

#pragma once

#include <linux/mali-c55-config.h>
#include <linux/videodev2.h>

#include <libipa/v4l2_params.h>

namespace libcamera {

namespace ipa::mali_c55 {

enum class MaliC55Blocks {
	Bls,
	AexpHist,
	AexpHistWeights,
	AexpIhist,
	AexpIhistWeights,
	Dgain,
	AwbGains,
	AwbConfig,
	MeshShadingConfig,
	MeshShadingSel,
};

namespace details {

template<MaliC55Blocks B>
struct block_type {
};

#define MALI_C55_DEFINE_BLOCK_TYPE(id, cfgType, blkType)		\
template<>								\
struct block_type<MaliC55Blocks::id> {					\
	using type = struct mali_c55_params_##cfgType;			\
	static constexpr mali_c55_param_block_type blockType = 		\
		mali_c55_param_block_type::MALI_C55_PARAM_##blkType;	\
};

MALI_C55_DEFINE_BLOCK_TYPE(Bls, sensor_off_preshading, BLOCK_SENSOR_OFFS)
MALI_C55_DEFINE_BLOCK_TYPE(AexpHist, aexp_hist, BLOCK_AEXP_HIST)
MALI_C55_DEFINE_BLOCK_TYPE(AexpHistWeights, aexp_weights,
			   BLOCK_AEXP_HIST_WEIGHTS)
MALI_C55_DEFINE_BLOCK_TYPE(AexpIhist, aexp_hist, BLOCK_AEXP_IHIST)
MALI_C55_DEFINE_BLOCK_TYPE(AexpIhistWeights, aexp_weights,
			   BLOCK_AEXP_IHIST_WEIGHTS)
MALI_C55_DEFINE_BLOCK_TYPE(Dgain, digital_gain, BLOCK_DIGITAL_GAIN)
MALI_C55_DEFINE_BLOCK_TYPE(AwbGains, awb_gains, BLOCK_AWB_GAINS)
MALI_C55_DEFINE_BLOCK_TYPE(AwbConfig, awb_config, BLOCK_AWB_CONFIG)
MALI_C55_DEFINE_BLOCK_TYPE(MeshShadingConfig, mesh_shading_config,
			   MESH_SHADING_CONFIG)
MALI_C55_DEFINE_BLOCK_TYPE(MeshShadingSel, mesh_shading_selection,
			   MESH_SHADING_SELECTION)

} /* namespace details */

class MaliC55Params : public V4L2Params<MaliC55Blocks>
{
public:
	static constexpr unsigned int kVersion = MALI_C55_PARAM_BUFFER_V1;

	MaliC55Params(Span<uint8_t> data)
		: V4L2Params<MaliC55Blocks>(data, kVersion)
	{
	}

	template<MaliC55Blocks B>
	auto block()
	{
		using Type = typename details::block_type<B>::type;

		auto blockType = details::block_type<B>::blockType;
		size_t blockSize = sizeof(Type);

		auto data = V4L2Params::block(B, blockType, blockSize);

		return V4L2ParamsBlock<Type>(data);
	}
};

} /* namespace ipa::mali_c55 */

} /* namespace libcamera */
