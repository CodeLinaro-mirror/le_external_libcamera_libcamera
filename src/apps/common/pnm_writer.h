/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2020, Raspberry Pi Ltd
 *
 * pnm_writer.h - PNM writer
 */

#pragma once

#include <libcamera/stream.h>

class PNMWriter
{
public:
	static int write(const char *filename,
			 const libcamera::StreamConfiguration &config,
			 const void *data);
};
