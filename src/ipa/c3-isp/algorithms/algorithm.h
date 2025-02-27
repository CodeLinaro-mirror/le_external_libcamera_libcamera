/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2024, Amlogic
 *
 * C3ISP control algorithm interface
 */

#pragma once

#include <libipa/algorithm.h>

#include "module.h"

namespace libcamera {

namespace ipa::c3isp {

class Algorithm : public libcamera::ipa::Algorithm<Module>
{
public:
	Algorithm()
	{
	}
};

} /* namespace ipa::c3isp */

} /* namespace libcamera */
