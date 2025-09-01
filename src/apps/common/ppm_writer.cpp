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
#include <stddef.h>
#include <vector>

#include <libcamera/formats.h>
#include <libcamera/pixel_format.h>

using namespace libcamera;

int PPMWriter::write(const char *filename,
		     const StreamConfiguration &config,
		     const Span<uint8_t> &data)
{
	size_t rPos, gPos, bPos, bytesPerPixel;
	switch (config.pixelFormat) {
	case libcamera::formats::R8:
		rPos = 0;
		gPos = 0;
		bPos = 0;
		bytesPerPixel = 1;
		break;
	case libcamera::formats::RGB888:
		rPos = 2;
		gPos = 1;
		bPos = 0;
		bytesPerPixel = 3;
		break;
	case libcamera::formats::BGR888:
		rPos = 0;
		gPos = 1;
		bPos = 2;
		bytesPerPixel = 3;
		break;
	case libcamera::formats::ARGB8888:
	case libcamera::formats::XRGB8888:
		rPos = 2;
		gPos = 1;
		bPos = 0;
		bytesPerPixel = 4;
		break;
	case libcamera::formats::RGBA8888:
	case libcamera::formats::RGBX8888:
		rPos = 3;
		gPos = 2;
		bPos = 1;
		bytesPerPixel = 4;
		break;
	case libcamera::formats::ABGR8888:
	case libcamera::formats::XBGR8888:
		rPos = 0;
		gPos = 1;
		bPos = 2;
		bytesPerPixel = 4;
		break;
	case libcamera::formats::BGRA8888:
	case libcamera::formats::BGRX8888:
		rPos = 1;
		gPos = 2;
		bPos = 3;
		bytesPerPixel = 4;
		break;
	default:
		std::cerr << "Only RGB output pixel formats are supported ("
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
	const bool transform = config.pixelFormat != formats::BGR888;
	std::vector<char> transformedRow(transform ? rowLength : 0);

	for (unsigned int y = 0; y < config.size.height; y++, row += config.stride) {
		if (transform)
			for (unsigned int x = 0; x < config.size.width; x++) {
				transformedRow[x * 3] = row[x * bytesPerPixel + rPos];
				transformedRow[x * 3 + 1] = row[x * bytesPerPixel + gPos];
				transformedRow[x * 3 + 2] = row[x * bytesPerPixel + bPos];
			}

		output.write((transform ? transformedRow.data() : row), rowLength);
		if (!output) {
			std::cerr << "Failed to write image data at row " << y << std::endl;
			return -EIO;
		}
	}

	return 0;
}
