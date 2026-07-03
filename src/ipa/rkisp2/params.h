/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2026, Ideas On Board
 *
 * RkISP2 ISP Parameters
 */

#pragma once

#include <stdint.h>

#include <linux/rkisp2-config.h>
#include <linux/videodev2.h>

#include <libipa/v4l2_params.h>

namespace libcamera {

namespace ipa::rkisp2 {

enum class RkISP2Blocks : uint16_t {
	Bls,
	AwbGains,
	Csm,
	Ccm,
	Goc,
	Lsc,
	AeLite,
	HistLite,
	HistBig0,
	HistBig1,
	HistBig2,
	AwbMeas,
};

namespace details {

template<RkISP2Blocks B>
struct block_type {
};

#define RKISP2_DEFINE_BLOCK_TYPE(id, cfgType, blkType)		\
template<>								\
struct block_type<RkISP2Blocks::id> {					\
	using type = struct rkisp2_params_##cfgType;			\
	static constexpr rkisp2_params_block_type blockType = 		\
		rkisp2_params_block_type::RKISP2_PARAMS_##blkType;	\
}

RKISP2_DEFINE_BLOCK_TYPE(Bls,			bls,		BLOCK_BLS);
RKISP2_DEFINE_BLOCK_TYPE(AwbGains,		awb_gains,	BLOCK_AWB_GAINS);
RKISP2_DEFINE_BLOCK_TYPE(Csm,			csm,		BLOCK_CSM);
RKISP2_DEFINE_BLOCK_TYPE(Ccm,			ccm,		BLOCK_CCM);
RKISP2_DEFINE_BLOCK_TYPE(Goc,			goc,		BLOCK_GOC);
RKISP2_DEFINE_BLOCK_TYPE(Lsc,			lsc,		BLOCK_LSC);
RKISP2_DEFINE_BLOCK_TYPE(AeLite,		ae_lite,	BLOCK_AE_LITE);
RKISP2_DEFINE_BLOCK_TYPE(HistLite,		hist_lite,	BLOCK_HIST_LITE);
RKISP2_DEFINE_BLOCK_TYPE(HistBig0,		hist_big,	BLOCK_HIST_BIG0);
RKISP2_DEFINE_BLOCK_TYPE(HistBig1,		hist_big,	BLOCK_HIST_BIG1);
RKISP2_DEFINE_BLOCK_TYPE(HistBig2,		hist_big,	BLOCK_HIST_BIG2);
RKISP2_DEFINE_BLOCK_TYPE(AwbMeas,		awb_meas,	BLOCK_AWB_MEAS);

struct param_traits {
	using id_type = RkISP2Blocks;

	template<id_type Id>
	using id_to_details = block_type<Id>;
};

} /* namespace details */

class RkISP2Params : public V4L2Params<details::param_traits>
{
public:
	RkISP2Params(Span<uint8_t> data)
		: V4L2Params(data, V4L2_ISP_PARAMS_VERSION_V1)
	{
	}
};

} /* namespace ipa::rkisp2 */

} /* namespace libcamera */
