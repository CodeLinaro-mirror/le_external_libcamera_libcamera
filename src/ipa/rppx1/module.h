/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2026 Renesas Electronics Corp.
 * Copyright (C) 2026 Ideas on Board Oy
 * Copyright (C) 2026 Ragnatech AB
 *
 * RPP-X1 IPA Module
 */

#pragma once

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
