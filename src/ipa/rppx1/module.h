/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2026, Ideas On Board
 *
 * RPP-X1 IPA Module
 */

#pragma once

#include <linux/media/dreamchip/rppx1-config.h>

#include <libcamera/ipa/rppx1_ipa_interface.h>

#include <libipa/module.h>

#include "ipa_context.h"
#include "params.h"
#include "stats.h"

namespace libcamera {

namespace ipa::rppx1 {

using Module = ipa::Module<IPAContext, IPAFrameContext, IPACameraSensorInfo,
			   RppX1Params, RppX1Stats>;

} /* namespace ipa::rppx1 */

} /* namespace libcamera*/
