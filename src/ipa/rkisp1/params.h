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

#define RKISP1_DEFINE_BLOCK_TYPE(blockType, blockStruct, id)		\
template<>								\
struct block_type<BlockType::blockType> {				\
	using type = struct rkisp1_cif_isp_##blockStruct##_config;	\
	static constexpr rkisp1_ext_params_block_type blockType =	\
		RKISP1_EXT_PARAMS_BLOCK_TYPE_##id;			\
};

RKISP1_DEFINE_BLOCK_TYPE(Bls, bls, BLS)
RKISP1_DEFINE_BLOCK_TYPE(Dpcc, dpcc, DPCC)
RKISP1_DEFINE_BLOCK_TYPE(Sdg, sdg, SDG)
RKISP1_DEFINE_BLOCK_TYPE(AwbGain, awb_gain, AWB_GAIN)
RKISP1_DEFINE_BLOCK_TYPE(Flt, flt, FLT)
RKISP1_DEFINE_BLOCK_TYPE(Bdm, bdm, BDM)
RKISP1_DEFINE_BLOCK_TYPE(Ctk, ctk, CTK)
RKISP1_DEFINE_BLOCK_TYPE(Goc, goc, GOC)
RKISP1_DEFINE_BLOCK_TYPE(Dpf, dpf, DPF)
RKISP1_DEFINE_BLOCK_TYPE(DpfStrength, dpf_strength, DPF_STRENGTH)
RKISP1_DEFINE_BLOCK_TYPE(Cproc, cproc, CPROC)
RKISP1_DEFINE_BLOCK_TYPE(Ie, ie, IE)
RKISP1_DEFINE_BLOCK_TYPE(Lsc, lsc, LSC)
RKISP1_DEFINE_BLOCK_TYPE(Awb, awb_meas, AWB_MEAS)
RKISP1_DEFINE_BLOCK_TYPE(Hst, hst, HST_MEAS)
RKISP1_DEFINE_BLOCK_TYPE(Aec, aec, AEC_MEAS)
RKISP1_DEFINE_BLOCK_TYPE(Afc, afc, AFC_MEAS)
RKISP1_DEFINE_BLOCK_TYPE(CompandBls, compand_bls, COMPAND_BLS)
RKISP1_DEFINE_BLOCK_TYPE(CompandExpand, compand_curve, COMPAND_EXPAND)
RKISP1_DEFINE_BLOCK_TYPE(CompandCompress, compand_curve, COMPAND_COMPRESS)

struct params_traits {
	using id_type = BlockType;

	template<id_type Id>
	using id_to_details = block_type<Id>;
};

} /* namespace details */

template<typename T>
class RkISP1ParamsBlock;

class RkISP1Params : public V4L2Params<details::params_traits>
{
public:
	static constexpr unsigned int kVersion = RKISP1_EXT_PARAM_BUFFER_V1;

	RkISP1Params(uint32_t format, Span<uint8_t> data)
		: V4L2Params(data, kVersion), format_(format)
	{
		if (format_ == V4L2_META_FMT_RK_ISP1_PARAMS) {
			memset(data.data(), 0, data.size());
			used_ = sizeof(struct rkisp1_params_cfg);
		}
	}

	template<details::params_traits::id_type id>
	auto block()
	{
		using Type = typename details::block_type<id>::type;

		return RkISP1ParamsBlock<Type>(this, id, block(id));
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
