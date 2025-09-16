/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2025, Ideas On Board
 *
 * V4L2 Parameters
 */

#pragma once

#include <map>
#include <stdint.h>
#include <string.h>

#include <linux/media/v4l2-isp.h>

#include <libcamera/base/span.h>

namespace libcamera {

namespace ipa {

template<typename T>
class V4L2ParamsBlock
{
public:
	V4L2ParamsBlock(const Span<uint8_t> data)
		: header_(data.subspan(0, sizeof(v4l2_params_block_header))),
		  data_(data.subspan(sizeof(v4l2_params_block_header)))
	{
	}

	void setEnabled(bool enabled)
	{
		struct v4l2_params_block_header *header =
			reinterpret_cast<struct v4l2_params_block_header *>(header_.data());

		header->flags &= ~(V4L2_PARAMS_FL_BLOCK_ENABLE |
				   V4L2_PARAMS_FL_BLOCK_DISABLE);
		header->flags |= enabled ? V4L2_PARAMS_FL_BLOCK_ENABLE
					 : V4L2_PARAMS_FL_BLOCK_DISABLE;
	}

	Span<uint8_t> header() const { return header_; }
	Span<uint8_t> data() const { return data_; }

	const T *operator->() const
	{
		return reinterpret_cast<const T *>(data().data());
	}

	T *operator->()
	{
		return reinterpret_cast<T *>(data().data());
	}

	const T &operator*() const
	{
		return *reinterpret_cast<const T *>(data().data());
	}

	T &operator*()
	{
		return *reinterpret_cast<T *>(data().data());
	}

private:
	Span<uint8_t> header_;
	Span<uint8_t> data_;
};

template<typename Traits>
class V4L2Params
{
public:
	V4L2Params(Span<uint8_t> data, unsigned int version)
		: data_(data)
	{
		struct v4l2_params_buffer *cfg =
			reinterpret_cast<struct v4l2_params_buffer *>(data_.data());
		cfg->data_size = 0;
		cfg->version = version;
		used_ = offsetof(struct v4l2_params_buffer, data);
	}

	size_t size() const { return used_; }

	template<typename Traits::id_type Id>
	auto block()
	{
		using Details = typename Traits::template id_to_details<Id>;

		using Type = typename Details::type;
		constexpr auto kernelId = Details::blockType;

		auto data = block(Id, kernelId, sizeof(Type));
		return V4L2ParamsBlock<Type>(data);
	}

protected:
	Span<uint8_t> block(typename Traits::id_type type,
			    unsigned int blockType, size_t blockSize)
	{
		/*
		 * Look up the block in the cache first. If an algorithm
		 * requests the same block type twice, it should get the same
		 * block.
		 */
		auto cacheIt = blocks_.find(type);
		if (cacheIt != blocks_.end())
			return cacheIt->second;

		/* Make sure we don't run out of space. */
		if (blockSize > data_.size() - used_)
			return {};

		/* Allocate a new block, clear its memory, and initialize its header. */
		Span<uint8_t> block = data_.subspan(used_, blockSize);
		used_ += blockSize;

		struct v4l2_params_buffer *cfg =
			reinterpret_cast<struct v4l2_params_buffer *>(data_.data());
		cfg->data_size += blockSize;

		memset(block.data(), 0, block.size());

		struct v4l2_params_block_header *header =
			reinterpret_cast<struct v4l2_params_block_header *>(block.data());
		header->type = blockType;
		header->size = block.size();

		/* Update the cache. */
		blocks_[type] = block;

		return block;
	}

	Span<uint8_t> data_;
	size_t used_;

	std::map<typename Traits::id_type, Span<uint8_t>> blocks_;
};

} /* namespace ipa */

} /* namespace libcamera */
