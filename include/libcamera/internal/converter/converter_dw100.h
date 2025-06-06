/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2024, Ideas On Board Oy
 *
 * i.MX8MP Dewarp Engine integration
 */

#pragma once

#include "libcamera/internal/converter/converter_dw100_vertexmap.h"
#include "libcamera/internal/converter/converter_v4l2_m2m.h"

namespace libcamera {

class MediaDevice;
class Rectangle;
class Stream;

class ConverterDW100 : public V4L2M2MConverter
{
public:
	ConverterDW100(std::shared_ptr<MediaDevice> media);

	int applyVertexMap(const Stream *stream, const V4L2Request *request = nullptr);
	Dw100VertexMap &vertexMap(const Stream *stream);

private:
	std::unique_ptr<V4L2M2MStream> makeStream(const Stream *stream) override;
	class DW100Stream : public V4L2M2MConverter::V4L2M2MStream
	{
	public:
		DW100Stream(V4L2M2MConverter *converter, const Stream *stream);

		int configure(const StreamConfiguration &inputCfg,
			      const StreamConfiguration &outputCfg) override;

		int applyVertexMap(const V4L2Request *request = nullptr);

		Dw100VertexMap vertexMap_;
	};
};

} /* namespace libcamera */
