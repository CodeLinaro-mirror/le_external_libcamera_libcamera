/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2024, Amlogic
 *
 * C3ISP IPA Module
 */

#pragma once

#include <linux/c3-isp-config.h>

#include <libcamera/ipa/c3isp_ipa_interface.h>

#include <libipa/module.h>

#include "ipa_context.h"
#include "params.h"

namespace libcamera {

namespace ipa::c3isp {

using Module = ipa::Module<IPAContext, IPAFrameContext, IPACameraSensorInfo,
			   C3ISPParams, c3_isp_stats_buffer>;

} /* namespace ipa::c3isp */

} /* namespace libcamera*/
