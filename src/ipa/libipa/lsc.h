/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2026 Ideas on Board Oy
 *
 * libIPA Lsc algorithm
 */

#pragma once

namespace libcamera {

namespace ipa {

namespace lsc {

struct ActiveState {
	bool enabled;
};

struct FrameContext {
	bool enabled;
	bool update;
};

} /* namespace lsc */

} /* namespace ipa */

} /* namespace libcamera */
