/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2026, Ideas On Board
 *
 * V4L2 Stats
 */

#pragma once

#include <stdint.h>

#include <linux/media/v4l2-isp.h>

#include <libcamera/base/log.h>
#include <libcamera/base/span.h>

namespace libcamera {

LOG_DECLARE_CATEGORY(V4L2Stats)

namespace ipa {

template<typename T>
class V4L2StatsBlock
{
public:
	V4L2StatsBlock(const Span<const uint8_t> data)
		: data_(data)
	{
	}

	virtual ~V4L2StatsBlock() {}

	virtual const T *operator->() const
	{
		return reinterpret_cast<const T *>(data_.data());
	}

	virtual const T &operator*() const
	{
		return *reinterpret_cast<const T *>(data_.data());
	}

	size_t size() const
	{
		return data_.size();
	}

protected:
	Span<const uint8_t> data_;
};

template<typename Traits>
class V4L2Stats
{
public:
	V4L2Stats(Span<uint8_t> data, unsigned int version)
		: data_(data)
	{
		struct v4l2_isp_buffer *stats =
			reinterpret_cast<struct v4l2_isp_buffer *>(data_.data());
		used_ = stats->data_size;

		if (version != stats->version)
			LOG(V4L2Stats, Error)
				<< "Unsupported v4l2-isp version: " << stats->version;
	}

	size_t bytesused() const { return used_; }

	template<typename Traits::id_type Id>
	auto block() const
	{
		using Details = typename Traits::template id_to_details<Id>;

		using Type = typename Details::type;
		constexpr auto kernelId = Details::blockType;

		auto data = block(kernelId, sizeof(Type));
		return V4L2StatsBlock<Type>(data);
	}

protected:
	const Span<const uint8_t> block(unsigned int blockType, size_t blockSize) const
	{
		struct v4l2_isp_buffer *stats =
			reinterpret_cast<struct v4l2_isp_buffer *>(data_.data());

		__u8 *data = stats->data;
		while (data < stats->data + stats->data_size) {
			struct v4l2_isp_block_header *header =
				reinterpret_cast<struct v4l2_isp_block_header *>(data);

			if (header->type != blockType) {
				data += header->size;
				continue;
			}

			if (header->size != blockSize) {
				LOG(V4L2Stats, Error)
					<< "Block type " << blockType
					<< " size mistmatch: expected "
					<< blockSize << " got:"
					<< header->size;
				return {};
			}

			Span<const uint8_t> block(data, header->size);
			return block;
		}

		LOG(V4L2Stats, Error) << "Unsupported stats block type: "
				      << blockType;

		return {};
	}

	Span<uint8_t> data_;
	size_t used_;
};

} /* namespace ipa */

} /* namespace libcamera */
