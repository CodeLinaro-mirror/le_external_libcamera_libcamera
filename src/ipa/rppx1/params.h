/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2026, Ideas On Board
 *
 * RPP-X1 ISP Parameters
 */

#pragma once

#include <linux/media/dreamchip/rppx1-config.h>

#include <libipa/v4l2_params.h>

namespace libcamera {

namespace ipa::rppx1 {

enum class BlockType : uint16_t {
	BlsPre1,
};

namespace details {

template<BlockType B>
struct block_type {
};

#define RPPX1_DEFINE_BLOCK_TYPE(blkType, cfgType, id)			\
	template<>							\
	struct block_type<BlockType::blkType> {				\
		using type = struct rppx1_##cfgType##_params;		\
		static constexpr rppx1_params_block_type blockType =	\
			RPPX1_PARAMS_BLOCK_TYPE_##id;			\
	};

RPPX1_DEFINE_BLOCK_TYPE(BlsPre1, bls, BLS_PRE1)

struct params_traits {
	using id_type = BlockType;

	template<id_type Id>
	using id_to_details = block_type<Id>;
};

} /* namespace details */

template<typename T>
class RppX1ParamsBlock final : public V4L2ParamsBlock<T>
{
public:
	RppX1ParamsBlock(const std::span<uint8_t> data)
		: V4L2ParamsBlock<T>(data),
		  configData_(data.subspan(sizeof(v4l2_isp_block_header)))
	{
	}

	const T *operator->() const override
	{
		return reinterpret_cast<const T *>(configData_.data());
	}

	T *operator->() override
	{
		return reinterpret_cast<T *>(configData_.data());
	}

	const T &operator*() const override
	{
		return *reinterpret_cast<const T *>(configData_.data());
	}

	T &operator*() override
	{
		return *reinterpret_cast<T *>(configData_.data());
	}

private:
	std::span<uint8_t> configData_;
};

class RppX1Params : public V4L2Params<details::params_traits>
{
public:
	RppX1Params(std::span<uint8_t> data)
		: V4L2Params(data, V4L2_ISP_PARAMS_VERSION_V1)
	{
	}
};

} /* namespace ipa::rppx1 */

} /* namespace libcamera */
