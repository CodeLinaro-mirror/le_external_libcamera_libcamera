/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2026, Ideas On Board
 *
 * RPP-X1 ISP Statistics
 */

#pragma once

#include <linux/media/dreamchip/rppx1-config.h>

#include <libipa/v4l2_stats.h>

namespace libcamera {

namespace ipa::rppx1 {

enum class StatsType : uint16_t {
	HistPost,
	ExmPre1,
	WbmeasPost,
};

namespace details {

template<StatsType B>
struct stats_type {
};

#define RPPX1_DEFINE_STATS_TYPE(blkType, cfgType, id)		\
template<>							\
struct stats_type<StatsType::blkType> {				\
	using type = struct rppx1_##cfgType##_stats;		\
	static constexpr rppx1_stats_block_type blockType =	\
		RPPX1_STATS_BLOCK_TYPE_##id;			\
};

RPPX1_DEFINE_STATS_TYPE(HistPost, hist, HIST_POST)
RPPX1_DEFINE_STATS_TYPE(ExmPre1, exm, EXM_PRE1)
RPPX1_DEFINE_STATS_TYPE(WbmeasPost, wbmeas, WBMEAS_POST)

struct stats_traits {
	using id_type = StatsType;

	template<id_type Id>
	using id_to_details = stats_type<Id>;
};

} /* namespace details */

class RppX1Stats : public V4L2Stats<details::stats_traits>
{
public:
	RppX1Stats(Span<uint8_t> data)
		: V4L2Stats(data, V4L2_ISP_VERSION_V1)
	{
	}
};

} /* namespace ipa::rppx1 */

} /* namespace libcamera */
