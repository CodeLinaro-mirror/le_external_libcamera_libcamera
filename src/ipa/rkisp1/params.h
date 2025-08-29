/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2024, Ideas On Board
 *
 * RkISP1 ISP Parameters
 */

#pragma once

#include <linux/rkisp1-config.h>
#include <linux/videodev2.h>

#include <libipa/v4l2_params.h>

namespace libcamera {

namespace ipa::rkisp1 {

enum class BlockType {
	Bls,
	Dpcc,
	Sdg,
	AwbGain,
	Flt,
	Bdm,
	Ctk,
	Goc,
	Dpf,
	DpfStrength,
	Cproc,
	Ie,
	Lsc,
	Awb,
	Hst,
	Aec,
	Afc,
	CompandBls,
	CompandExpand,
	CompandCompress,
};

namespace details {

template<BlockType B>
struct block_type {
};

#define RKISP1_DEFINE_BLOCK_TYPE(blockType, blockStruct)		\
template<>								\
struct block_type<BlockType::blockType> {				\
	using type = struct rkisp1_cif_isp_##blockStruct##_config;	\
};

RKISP1_DEFINE_BLOCK_TYPE(Bls, bls)
RKISP1_DEFINE_BLOCK_TYPE(Dpcc, dpcc)
RKISP1_DEFINE_BLOCK_TYPE(Sdg, sdg)
RKISP1_DEFINE_BLOCK_TYPE(AwbGain, awb_gain)
RKISP1_DEFINE_BLOCK_TYPE(Flt, flt)
RKISP1_DEFINE_BLOCK_TYPE(Bdm, bdm)
RKISP1_DEFINE_BLOCK_TYPE(Ctk, ctk)
RKISP1_DEFINE_BLOCK_TYPE(Goc, goc)
RKISP1_DEFINE_BLOCK_TYPE(Dpf, dpf)
RKISP1_DEFINE_BLOCK_TYPE(DpfStrength, dpf_strength)
RKISP1_DEFINE_BLOCK_TYPE(Cproc, cproc)
RKISP1_DEFINE_BLOCK_TYPE(Ie, ie)
RKISP1_DEFINE_BLOCK_TYPE(Lsc, lsc)
RKISP1_DEFINE_BLOCK_TYPE(Awb, awb_meas)
RKISP1_DEFINE_BLOCK_TYPE(Hst, hst)
RKISP1_DEFINE_BLOCK_TYPE(Aec, aec)
RKISP1_DEFINE_BLOCK_TYPE(Afc, afc)
RKISP1_DEFINE_BLOCK_TYPE(CompandBls, compand_bls)
RKISP1_DEFINE_BLOCK_TYPE(CompandExpand, compand_curve)
RKISP1_DEFINE_BLOCK_TYPE(CompandCompress, compand_curve)

} /* namespace details */

template<typename T>
class RkISP1ParamsBlock;

class RkISP1Params : public V4L2Params<BlockType>
{
public:
	static constexpr unsigned int kVersion = RKISP1_EXT_PARAM_BUFFER_V1;

	RkISP1Params(uint32_t format, Span<uint8_t> data)
		: V4L2Params<BlockType>(data, kVersion), format_(format)
	{
		if (format_ == V4L2_META_FMT_RK_ISP1_PARAMS) {
			memset(data.data(), 0, data.size());
			used_ = sizeof(struct rkisp1_params_cfg);
		}
	}

	template<BlockType B>
	auto block()
	{
		using Type = typename details::block_type<B>::type;

		return RkISP1ParamsBlock<Type>(this, B, block(B));
	}

	uint32_t format() const { return format_; }
	void setBlockEnabled(BlockType type, bool enabled);

private:
	Span<uint8_t> block(BlockType type);

	uint32_t format_;
};

template<typename T>
class RkISP1ParamsBlock : public V4L2ParamsBlock<T>
{
public:
	RkISP1ParamsBlock(RkISP1Params *params, BlockType type,
			  const Span<uint8_t> &data)
		: V4L2ParamsBlock<T>(data)
	{
		params_ = params;
		type_ = type;

		/* Legacy param format has no header */
		if (params_->format() == V4L2_META_FMT_RK_ISP1_PARAMS)
			data_ = data;
	}

	void setEnabled(bool enabled)
	{
		/*
		 * For the legacy fixed format, blocks are enabled in the
		 * top-level header. Delegate to the RkISP1Params class.
		 */
		if (params_->format() == V4L2_META_FMT_RK_ISP1_PARAMS)
			return params_->setBlockEnabled(type_, enabled);

		return V4L2ParamsBlock<T>::setEnabled(enabled);
	}

private:
	RkISP1Params *params_;
	BlockType type_;
	Span<uint8_t> data_;
};

} /* namespace ipa::rkisp1 */

} /* namespace libcamera*/
