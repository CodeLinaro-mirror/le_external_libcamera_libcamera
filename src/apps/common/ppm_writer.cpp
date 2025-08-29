/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2024 Red Hat, Inc.
 *
 * PPM writer
 */

#include "ppm_writer.h"

#include <errno.h>
#include <fstream>
#include <iostream>
#include <vector>

#include <libcamera/formats.h>
#include <libcamera/pixel_format.h>

using namespace libcamera;

int PPMWriter::write(const char *filename,
		     const StreamConfiguration &config,
		     const Span<uint8_t> &data)
{
	if (config.pixelFormat != formats::BGR888 &&
	    config.pixelFormat != formats::XBGR8888) {
		std::cerr
			<< "Only BGR888 and XBGR8888 output pixel formats are supported ("
			<< config.pixelFormat << " requested)" << std::endl;
		return -EINVAL;
	}

	std::ofstream output(filename, std::ios::binary);
	if (!output) {
		std::cerr << "Failed to open ppm file: " << filename << std::endl;
		return -EIO;
	}

	output << "P6" << std::endl
	       << config.size.width << " " << config.size.height << std::endl
	       << "255" << std::endl;
	if (!output) {
		std::cerr << "Failed to write the file header" << std::endl;
		return -EIO;
	}

	const unsigned int rowLength = config.size.width * 3;
	const char *row = reinterpret_cast<const char *>(data.data());
	const bool transform = config.pixelFormat == formats::XBGR8888;
	std::vector<char> transformedRow(transform ? rowLength : 0);

	for (unsigned int y = 0; y < config.size.height; y++, row += config.stride) {
		if (transform)
			for (unsigned int x = 0; x < config.size.width; x++) {
				transformedRow[x * 3] = row[x * 4];
				transformedRow[x * 3 + 1] = row[x * 4 + 1];
				transformedRow[x * 3 + 2] = row[x * 4 + 2];
			}

		output.write((transform ? transformedRow.data() : row), rowLength);
		if (!output) {
			std::cerr << "Failed to write image data at row " << y << std::endl;
			return -EIO;
		}
	}

	return 0;
}
