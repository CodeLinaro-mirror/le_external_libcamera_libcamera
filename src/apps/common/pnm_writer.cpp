/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2024 Red Hat, Inc.
 *
 * pnm_writer.cpp - PNM writer
 */

#include "pnm_writer.h"

#include <fstream>
#include <iostream>

#include <libcamera/formats.h>
#include <libcamera/pixel_format.h>

using namespace libcamera;

int PNMWriter::write(const char *filename,
		     const StreamConfiguration &config,
		     const void *data)
{
	if (config.pixelFormat != formats::BGR888) {
		std::cerr << "Only BGR888 output pixel format is supported ("
			  << config.pixelFormat << " requested)" << std::endl;
		return -EINVAL;
	}

	std::ofstream output(filename, std::ios::binary);
	if (!output) {
		std::cerr << "Failed to open pnm file: " << filename << std::endl;
		return -EINVAL;
	}

	output << "P6" << std::endl
	       << config.size.width << " " << config.size.height << std::endl
	       << "255" << std::endl;
	if (!output) {
		std::cerr << "Failed to write the file header" << std::endl;
		return -EINVAL;
	}

	const unsigned int rowLength = config.size.width * 3;
	const unsigned int paddedRowLength = config.stride;
	const char *row = reinterpret_cast<const char *>(data);
	for (unsigned int y = 0; y < config.size.height; y++, row += paddedRowLength) {
		output.write(row, rowLength);
		if (!output) {
			std::cerr << "Failed to write image data at row " << y << std::endl;
			return -EINVAL;
		}
	}

	return 0;
}
