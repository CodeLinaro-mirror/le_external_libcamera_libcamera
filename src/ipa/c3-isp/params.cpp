/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2024, Amlogic Inc.
 *
 * C3ISP ISP Parameters
 */

#include "params.h"

#include <map>
#include <stddef.h>
#include <string.h>

#include <linux/c3-isp-config.h>
#include <linux/videodev2.h>

#include <libcamera/base/log.h>
#include <libcamera/base/utils.h>

namespace libcamera {

LOG_DEFINE_CATEGORY(C3ISPParams)

namespace ipa::c3isp {

namespace {

struct BlockTypeInfo {
	enum c3_isp_params_block_type type;
	size_t size;
};

#define C3ISP_BLOCK_TYPE_ENTRY(block, id, type)                      \
	{                                                            \
		BlockType::block,                                    \
		{                                                    \
			C3_ISP_PARAMS_BLOCK_##id,                    \
				sizeof(struct c3_isp_params_##type), \
		}                                                    \
	}

const std::map<BlockType, BlockTypeInfo> kBlockTypeInfo = {
	C3ISP_BLOCK_TYPE_ENTRY(AWBGains, AWB_GAINS, awb_gains),
	C3ISP_BLOCK_TYPE_ENTRY(AWBConfig, AWB_CONFIG, awb_config),
	C3ISP_BLOCK_TYPE_ENTRY(AEConfig, AE_CONFIG, ae_config),
	C3ISP_BLOCK_TYPE_ENTRY(AFConfig, AF_CONFIG, af_config),
	C3ISP_BLOCK_TYPE_ENTRY(PostGamma, PST_GAMMA, pst_gamma),
	C3ISP_BLOCK_TYPE_ENTRY(Ccm, CCM, ccm),
	C3ISP_BLOCK_TYPE_ENTRY(Csc, CSC, csc),
	C3ISP_BLOCK_TYPE_ENTRY(Blc, BLC, blc),
};

} /* namespace */

C3ISPParamsBlockBase::C3ISPParamsBlockBase(BlockType type,
					   const Span<uint8_t> &data)
	: type_(type), data_(data)
{
	header_ = data.subspan(0, sizeof(c3_isp_params_block_header));
}

void C3ISPParamsBlockBase::setEnabled(uint16_t flags)
{
	struct c3_isp_params_block_header *header =
		reinterpret_cast<struct c3_isp_params_block_header *>(header_.data());

	header->flags = flags;
}

C3ISPParams::C3ISPParams(Span<uint8_t> data)
	: data_(data), used_(0)
{
	struct c3_isp_params_cfg *buffer =
		reinterpret_cast<struct c3_isp_params_cfg *>(data.data());

	buffer->version = C3_ISP_PARAMS_BUFFER_V0;
	buffer->data_size = 0;

	used_ += offsetof(struct c3_isp_params_cfg, data);
}

Span<uint8_t> C3ISPParams::block(BlockType type)
{
	auto infoIt = kBlockTypeInfo.find(type);
	if (infoIt == kBlockTypeInfo.end()) {
		LOG(C3ISPParams, Error)
			<< "Invalid parameters type "
			<< utils::to_underlying(type);
		return {};
	}

	const BlockTypeInfo &info = infoIt->second;

	auto cacheIt = blocks_.find(type);
	if (cacheIt != blocks_.end())
		return cacheIt->second;

	size_t size = info.size;
	if (size > data_.size() - used_) {
		LOG(C3ISPParams, Error)
			<< "No enough remaining space "
			<< utils::to_underlying(type);
		return {};
	}

	Span<uint8_t> block = data_.subspan(used_, info.size);
	used_ += block.size();

	struct c3_isp_params_cfg *buffer =
		reinterpret_cast<struct c3_isp_params_cfg *>(data_.data());
	buffer->data_size += block.size();

	memset(block.data(), 0, block.size());

	struct c3_isp_params_block_header *header =
		reinterpret_cast<struct c3_isp_params_block_header *>(block.data());
	header->type = info.type;
	header->size = block.size();

	blocks_[type] = block;

	return block;
}

} /* namespace ipa::c3isp */

} /* namespace libcamera */
