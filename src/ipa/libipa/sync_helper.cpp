/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2025 Ideas on Board Oy
 *
 * Helper class that handles sync
 */
#include "sync_helper.h"

#include <algorithm>
#include <chrono>
#include <libcamera/controls.h>

/**
 * \file sync_helper.h
 * \brief Helper class that encapsulates handling sync
 */

namespace libcamera {

namespace ipa {

/**
 * \class SyncHelper
 * \brief Class for handling sync
 *
 * In order for a Camera to support the sync algorithm (via the sync layer), it
 * needs to implement the FrameDurationLimits control, the SyncAdjustment
 * control, and the SensorTimestamp metadata. The first is handled by AGC, the
 * last is handled by the IPA cores, and the second one is handled by this
 * helper. It must be plumbed into IPAs, however.
 */

/**
 * \fn SyncHelper::SyncHelper()
 * \brief Construct an SyncHelper instance
 */

/**
 * \fn SyncHelper::controlInfo(int64_t maxFrameDuration)
 * \brief Return an entry for the ControlInfoMap of the IPA for SyncAdjustment
 * \param[in] maxFrameDuration The maximum value for FrameDurationLimits
 *
 * This function creates an entry for SyncAdjustment that can be insterted
 * directly into the ControlInfoMap of the IPA.
 *
 * The SyncAdjustment limits are computed based on the \a maxFrameDuration.
 * Technically the limits of SyncAdjustment depend on the currently set
 * FrameDurationLimits, but since they can be set in the same request this
 * doesn't really work. Instead report half of the maximum FrameDurationLimits
 * as the SyncAdjustment limits.
 *
 * \return Entry for ControlInfoMap for SyncAdjustment
 */

/**
 * \fn SyncHelper::setSync(int32_t sync, utils::Duration minFrameDuration)
 * \brief Set the sync adjustment value
 * \param[in] sync The SyncAdjustment value as passed in by the application, in microseconds
 * \param[in] minFrameDuration The minimum frame duration as set by FrameDurationLimits
 *
 * This function takes the value of SyncAdjustment as passed in by the
 * application, and saves it to be later read by getSync(). The \a sync value
 * will be clamped by \a minFrameDuration. This
 * function is meant to be called by the IPA at queueRequest time.
 */

/**
 * \fn SyncHelper::getSync()
 * \brief Retrieve the set sync adjustment value
 *
 * This function returns the SyncAdjustment value stored in setSync(). It is
 * meant to be read out by the IPA when the computing the frame duration to
 * set, usually by AGC.
 *
 * \return The amount of time to adjust the frame duration by for sync
 */

/**
 * \fn SyncHelper::resetSync()
 * \brief Reset the stored sync adjustment value
 *
 * This function resets the state of the sync helper, that is it zeros the
 * stored frame duration offset.
 */

} /* namespace ipa */

} /* namespace libcamera */
