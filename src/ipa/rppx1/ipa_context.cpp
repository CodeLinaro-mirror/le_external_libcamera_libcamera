/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2026 Renesas Electronics Corp.
 * Copyright (C) 2026 Ideas on Board Oy
 * Copyright (C) 2026 Ragnatech AB
 *
 * RPP-X1 IPA Context
 */

#include "ipa_context.h"

/**
 * \file ipa_context.h
 * \brief Context and state information shared between the algorithms
 */

namespace libcamera::ipa::rppx1 {

/**
 * \struct IPASessionConfiguration
 * \brief Session configuration for the IPA module
 */

/**
 * \struct IPAActiveState
 * \brief Active state for algorithms
 */

/**
 * \struct IPAFrameContext
 * \brief Per-frame context for algorithms
 */

/**
 * \struct IPAContext
 * \brief Global IPA context data shared between all algorithms
 *
 * \var IPAContext::sensorInfo
 * \brief The IPA session sensorInfo, immutable during the session
 *
 * \var IPAContext::configuration
 * \brief The IPA session configuration, immutable during the session
 *
 * \var IPAContext::activeState
 * \brief The IPA active state, storing the latest state for all algorithms
 *
 * \var IPAContext::frameContexts
 * \brief Ring buffer of per-frame contexts
 *
 * \var IPAContext::ctrlMap
 * \brief The IPA map of controls
 *
 * \var IPAContext::camHelper
 * \brief The IPA camera helper
 */

/**
 * \fn IPAContext::IPAContext
 * \brief Construct the IPA context
 * \param[in] frameContextSize Size of the frame context queue
 */

} /* namespace libcamera::ipa::rppx1 */
