/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2023, Ideas on Board Oy.
 *
 * deprecated.h - Deprecated API reporting
 *
 * Deprecated features of the API will be exposed here for at least one release
 * iteration to ease reporting of API adjustments for applications.
 */

#pragma once

namespace libcamera {

/*
 * Deprectated following v0.0.5
 *
 * The use of StreamRoles indicates applications to use dynamic allocations
 * of the StreamRole when this is not always required.
 */
using StreamRoles [[deprecated("Use a span, array or vector directly")]]
	= std::vector<StreamRole>;

} /* namespace libcamera */
