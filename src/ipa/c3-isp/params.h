/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2024, Amlogic Inc.
 *
 * C3ISP ISP Parameters
 */

#pragma once

#include <map>
#include <stdint.h>

#include <linux/c3-isp-config.h>

#include <libcamera/base/class.h>
#include <libcamera/base/span.h>

namespace libcamera {

namespace ipa::c3isp {

enum class BlockType {
	AWBGains,
	AWBConfig,
	AEConfig,
	AFConfig,
	PostGamma,
	Ccm,
	Csc,
	Blc,
};

namespace details {

template<BlockType B>
struct block_type {
};

#define C3ISP_DEFINE_BLOCK_TYPE(blocktype, blockStruct)          \
	template<>                                               \
	struct block_type<BlockType::blocktype> {                \
		using type = struct c3_isp_params_##blockStruct; \
	};

C3ISP_DEFINE_BLOCK_TYPE(AWBGains, awb_gains)
C3ISP_DEFINE_BLOCK_TYPE(AWBConfig, awb_config)
C3ISP_DEFINE_BLOCK_TYPE(AEConfig, ae_config)
C3ISP_DEFINE_BLOCK_TYPE(AFConfig, af_config)
C3ISP_DEFINE_BLOCK_TYPE(PostGamma, pst_gamma)
C3ISP_DEFINE_BLOCK_TYPE(Ccm, ccm)
C3ISP_DEFINE_BLOCK_TYPE(Csc, csc)
C3ISP_DEFINE_BLOCK_TYPE(Blc, blc)

} /* namespace details */

class C3ISPParams;

class C3ISPParamsBlockBase
{
public:
	C3ISPParamsBlockBase(BlockType type, const Span<uint8_t> &data);

	Span<uint8_t> data() const { return data_; }

	void setEnabled(uint16_t flags);

private:
	LIBCAMERA_DISABLE_COPY(C3ISPParamsBlockBase)

	BlockType type_;
	Span<uint8_t> header_;
	Span<uint8_t> data_;
};

template<BlockType B>
class C3ISPParamsBlock : public C3ISPParamsBlockBase
{
public:
	using Type = typename details::block_type<B>::type;

	C3ISPParamsBlock(const Span<uint8_t> &data)
		: C3ISPParamsBlockBase(B, data)
	{
	}

	const Type *operator->() const
	{
		return reinterpret_cast<const Type *>(data().data());
	}

	Type *operator->()
	{
		return reinterpret_cast<Type *>(data().data());
	}

	const Type &operator*() const &
	{
		return *reinterpret_cast<const Type *>(data().data());
	}

	const Type &operator*() &
	{
		return *reinterpret_cast<Type *>(data().data());
	}
};

class C3ISPParams
{
public:
	C3ISPParams(Span<uint8_t> data);

	template<BlockType B>
	C3ISPParamsBlock<B> block()
	{
		return C3ISPParamsBlock<B>(block(B));
	}

	size_t size() const { return used_; }

private:
	friend class C3ISPParamsBlockBase;

	Span<uint8_t> block(BlockType type);

	Span<uint8_t> data_;
	size_t used_;

	std::map<BlockType, Span<uint8_t>> blocks_;
};

} /* namespace ipa::c3isp */

} /* namespace libcamera */
