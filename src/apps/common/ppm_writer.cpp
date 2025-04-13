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

#include <libcamera/formats.h>
#include <libcamera/pixel_format.h>

using namespace libcamera;

int PPMWriter::write(const char *filename,
		     const StreamConfiguration &config,
		     const Span<uint8_t> &data)
{
	int r_pos, g_pos, b_pos, bpp;
	switch (config.pixelFormat) {
	case libcamera::formats::R8:
		r_pos = 0;
		g_pos = 0;
		b_pos = 0;
		bpp = 1;
		break;
	case libcamera::formats::RGB888:
		r_pos = 2;
		g_pos = 1;
		b_pos = 0;
		bpp = 3;
		break;
	case libcamera::formats::BGR888:
		r_pos = 0;
		g_pos = 1;
		b_pos = 2;
		bpp = 3;
		break;
	case libcamera::formats::ARGB8888:
	case libcamera::formats::XRGB8888:
		r_pos = 2;
		g_pos = 1;
		b_pos = 0;
		bpp = 4;
		break;
	case libcamera::formats::RGBA8888:
	case libcamera::formats::RGBX8888:
		r_pos = 3;
		g_pos = 2;
		b_pos = 1;
		bpp = 4;
		break;
	case libcamera::formats::ABGR8888:
	case libcamera::formats::XBGR8888:
		r_pos = 0;
		g_pos = 1;
		b_pos = 2;
		bpp = 4;
		break;
	case libcamera::formats::BGRA8888:
	case libcamera::formats::BGRX8888:
		r_pos = 1;
		g_pos = 2;
		b_pos = 3;
		bpp = 4;
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
	// Output without any conversion will be slightly faster
	if (config.pixelFormat == formats::BGR888) {
		const char *row = reinterpret_cast<const char *>(data.data());
		for (unsigned int y = 0; y < config.size.height; y++, row += config.stride) {
			output.write(row, rowLength);
			if (!output) {
				std::cerr << "Failed to write image data at row " << y << std::endl;
				return -EIO;
			}
		}
		return 0;
	}

	const unsigned int inputRowBytes = config.stride; // should be >= width * 4
	// Create a temporary buffer to hold one output row.
	std::vector<char> rowBuffer(rowLength);
	const uint8_t *row = reinterpret_cast<const uint8_t *>(data.data());
	for (unsigned int y = 0; y < config.size.height; y++, row += inputRowBytes) {
		for (unsigned int x = 0; x < config.size.width; x++) {
			const uint8_t *pixel = row + x * bpp;
			rowBuffer[x * 3 + 0] = static_cast<char>(pixel[r_pos]);
			rowBuffer[x * 3 + 1] = static_cast<char>(pixel[g_pos]);
			rowBuffer[x * 3 + 2] = static_cast<char>(pixel[b_pos]);
		}
		output.write(rowBuffer.data(), rowLength);
		if (!output) {
			std::cerr << "Failed to write image data at row " << y << std::endl;
			return -EIO;
		}
	}
	return 0;
}
