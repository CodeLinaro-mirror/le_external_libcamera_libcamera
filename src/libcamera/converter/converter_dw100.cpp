/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2024, Ideas On Board Oy
 *
 * i.MX8MP Dewarp Engine integration
 */

#include "libcamera/internal/converter/converter_dw100.h"

#include <linux/dw100.h>

#include <libcamera/base/log.h>

#include <libcamera/geometry.h>
#include <libcamera/stream.h>

#include "libcamera/internal/media_device.h"
#include "libcamera/internal/v4l2_videodevice.h"

namespace libcamera {

LOG_DECLARE_CATEGORY(Converter)

/**
 * \class libcamera::ConverterDW100
 * \brief The i.MX8MP dewarp converter implements the converter interface based
 * on V4L2 M2M device.
*/

/**
 * \fn ConverterDW100::ConverterDW100
 * \brief Construct a ConverterDW100 instance
 * \param[in] media The media device implementing the converter
 */
ConverterDW100::ConverterDW100(std::shared_ptr<MediaDevice> media)
	: V4L2M2MConverter(media)
{
}

/**
 * \brief Apply the vertex map for a given stream
 * \param[in] stream The stream to update
 * \param[in] request An optional request
 *
 * This function updates the vertex map on the stream \a stream. If \a request
 * is provided, the updated happens in that request.
 *
 * \return 0 on success or a negative error code otherwise
 */
int ConverterDW100::applyVertexMap(const Stream *stream, const V4L2Request *request)
{
	auto iter = streams_.find(stream);
	if (iter == streams_.end())
		return -EINVAL;

	return dynamic_cast<DW100Stream *>(iter->second.get())->applyVertexMap(request);
}

/**
 * \brief Get the vertex map for a stream
 * \param[in] stream The stream
 *
 * This function returns a reference to the vertex map of the stream \a stream.
 *
 * \return The vertex map
 */
Dw100VertexMap &ConverterDW100::vertexMap(const Stream *stream)
{
	auto iter = streams_.find(stream);
	V4L2M2MConverter::V4L2M2MStream *s = iter->second.get();
	ASSERT(s);
	return dynamic_cast<DW100Stream *>(s)->vertexMap_;
}

std::unique_ptr<V4L2M2MConverter::V4L2M2MStream> ConverterDW100::makeStream(const Stream *stream)
{
	return std::unique_ptr<V4L2M2MConverter::V4L2M2MStream>(new DW100Stream(this, stream));
}

ConverterDW100::DW100Stream::DW100Stream(V4L2M2MConverter *converter,
					 const Stream *stream)
	: V4L2M2MConverter::V4L2M2MStream(converter, stream)
{
}

int ConverterDW100::DW100Stream::configure(const StreamConfiguration &inputCfg,
					   const StreamConfiguration &outputCfg)
{
	int ret;
	ret = V4L2M2MConverter::V4L2M2MStream::configure(inputCfg, outputCfg);
	if (ret)
		return ret;

	/* Todo: is that comment still valid?
	 For actual dewarping we need the active area of the sensor mode here. */
	vertexMap_.setInputSize(inputCfg.size);
	vertexMap_.setOutputSize(outputCfg.size);
	return 0;
}

int ConverterDW100::DW100Stream::applyVertexMap(const V4L2Request *request)
{
	std::vector<uint32_t> map = vertexMap_.getVertexMap();
	auto value = Span<const int32_t>(reinterpret_cast<const int32_t *>(&map[0]), map.size());

	ControlList ctrls;
	ctrls.set(V4L2_CID_DW100_DEWARPING_16x16_VERTEX_MAP, value);
	return applyControls(ctrls, request);
}

} /* namespace libcamera */
