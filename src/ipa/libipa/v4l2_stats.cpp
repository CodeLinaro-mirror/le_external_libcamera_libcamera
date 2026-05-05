/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2026, Ideas On Board
 *
 * V4L2 Statistics
 */

#include "v4l2_stats.h"

namespace libcamera {

LOG_DEFINE_CATEGORY(V4L2Stats)

namespace ipa {

/**
 * \file v4l2_stats.cpp
 * \brief Helper class to handle an ISP statistics buffer compatible with
 * the generic V4L2 ISP format
 *
 * The Linux kernel defines a generic buffer format for ISP statistics.
 * The format describes a serialisation method that allows userspace to
 * access statistics data from a binary buffer.
 *
 * The V4L2Stats class implements support the V4L2 ISP statistics buffer format
 * and allows users to retrieve an ISP statistics blocks, represented as
 * V4L2StatsBlock class instances.
 *
 * IPA implementations using this helpers should define an enumeration of ISP
 * blocks supported by the IPA module and use a set of common abstraction to
 * help their derived implementation of V4L2Stats translate the enumerated ISP
 * block identifiers to the actual type of the statistics data as defined by
 * the kernel interface.
 */

/**
 * \class V4L2StatsBlock
 * \brief Helper class that represents an ISP statistics block
 *
 * Each ISP measurement block produces a set of statistics data whose memory
 * layout is defined by the kernel interface.
 *
 * This class represents an ISP statistics block entry. It is constructed
 * with a reference to the memory area where the statistics block is
 * stored in the statistics buffer. The template parameter represents
 * the underlying kernel-defined ISP statistics block type and allows its
 * user to easily cast it to said type to read the statistics data.
 *
 * \sa V4L2Stats::block()
 */

/**
 * \fn V4L2StatsBlock::V4L2StatsBlock()
 * \brief Construct a V4L2StatsBlock with memory represented by \a data
 * \param[in] data The memory area where the statistics block is located
 */

/**
 * \fn V4L2StatsBlock::operator->() const
 * \brief Access the statistics block casting it to the kernel-defined
 * ISP block type
 *
 * The V4L2StatsBlock is templated with the kernel defined ISP block type. This
 * function allows users to easily cast a V4L2StatsBlock to the underlying
 * kernel-defined type in order to easily access the statistics data.
 *
 * \code{.cpp}
 *
 * // The kernel header defines the ISP statistics types, in example
 * // struct my_isp_awb_stats_data {
 * //		u16 mean_r;
 * //		u16 mean_g;
 * //		u16 mean_b;
 * //  }
 *
 * template<> V4L2StatsBlock<struct my_isp_awb_stats_data> awbStats = ...
 *
 * u16 mean_r = awbStats->mean_r;
 * u16 mean_g = awbStats->mean_g;
 * u16 mean_b = awbStats->mean_b;
 *
 * \endcode
 *
 * Users of this class shall not create a V4L2StatsBlock manually but should
 * use V4L2Stats::block().
 */

/**
 * \fn V4L2StatsBlock::operator*() const
 * \copydoc V4L2StatsBlock::operator->() const
 */

/**
 * \fn V4L2StatsBlock::size() const
 * \brief Retrieve the size (in bytes) of the ISP statistics block, including
 * the block's header
 * \return The size of the stats block
 */

/**
 * \var V4L2StatsBlock::data_
 * \brief Memory area where ISP statistics have been serialized
 */

/**
 * \class V4L2Stats
 * \brief Helper class that represent an ISP statistics buffer
 *
 * This class represents an ISP statistics buffer. It is constructed with a
 * reference to the memory mapped buffer that has been dequeued from the ISP
 * driver.
 *
 * This class is templated with the type of the enumeration of ISP blocks that
 * each IPA module is expected to support. IPA modules are expected to derive
 * this class by providing a 'stats_traits' type that helps the class associate
 * a block type with the actual memory area that represents the ISP statistics
 * block.
 *
 * \code{.cpp}
 *
 * // Define the supported ISP statistics blocks
 * enum class myISPStats {
 *	Agc,
 *	Awb,
 *	...
 * };
 *
 * // Maps the C++ enum type to the kernel enum type and concrete parameter type
 * template<myISPStats B>
 * struct block_type {
 * };
 *
 * template<>
 * struct block_type<myISPStats::Agc> {
 *	using type = struct my_isp_kernel_stats_type_agc;
 *	static constexpr kernel_enum_type blockType = MY_ISP_STATS_TYPE_AGC;
 * };
 *
 * template<>
 * struct block_type<myISPStats::Awb> {
 *	using type = struct my_isp_kernel_stats_type_awb;
 *	static constexpr kernel_enum_type blockType = MY_ISP_STATS_TYPE_AWB;
 * };
 *
 *
 * // Convenience type to associate a block id to the 'block_type' overload
 * struct stats_traits {
 * 	using id_type = myISPStats;
 * 	template<id_type Id> using id_to_details = block_type<Id>;
 * };
 *
 * ...
 *
 * // Derive the V4L2Stats class by providing stats_traits
 * class MyISPStats : public V4L2Stats<stats_traits>
 * {
 * public:
 * 	MyISPStats::MyISPStats(Span<uint8_t> data)
 * 		: V4L2Stats(data)
 * 	{
 * 	}
 * };
 *
 * \endcode
 *
 * Users of this class can then easily access an ISP statistics block as a
 * V4L2StatsBlock instance.
 *
 * \code{.cpp}
 *
 * MyISPStats stats(data);
 *
 * auto awb = stats.block<myISPStats::AWB>();
 * auto mean_r = awb->mean_r;
 * auto mean_g = awb->mean_g;
 * auto mean_b = awb->mean_b;
 * \endcode
 */

/**
 * \fn V4L2Stats::V4L2Stats()
 * \brief Construct an instance of V4L2Stats
 * \param[in] data Reference to the v4l2-buffer memory mapped area
 * \param[in] version The expected V4L2 ISP serialization format version
 */

/**
 * \fn V4L2Stats::bytesused()
 * \brief Retrieve the size of the statistics buffer (in bytes)
 * \return The number of bytes occupied by the ISP statistics
 */

/**
 * \fn V4L2Stats::block() const
 * \brief Retrieve the location of an ISP statistics block a return it
 * \return A V4L2StatsBlock instance that points to the ISP statistics block
 */

/**
 * \fn V4L2Stats::block(unsigned int blockType, size_t blockSize) const
 * \brief Retrieve an ISP statistics block a returns a reference to it
 * \param[in] blockType The kernel-defined ISP block identifier, used to
 * identify the block header
 * \param[in] blockSize The ISP statistics block size, for validation
 *
 * Parse the buffer of serialized statistics data and retrieve the location
 * of the block with type \a blockType of size \a blockSize.
 *
 * IPA modules that derive the V4L2Stats class shall use this function to
 * retrieve the memory area that will be used to construct a V4L2StatsBlock<T>
 * before returning it to the caller.
 */

/**
 * \var V4L2Stats::data_
 * \brief The ISP statistics buffer memory
 */

/**
 * \var V4L2Stats::used_
 * \brief The number of bytes used by the statistics buffer
 */

} /* namespace ipa */

} /* namespace libcamera */
