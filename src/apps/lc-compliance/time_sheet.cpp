/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2024, Ideas on Board Oy
 *
 * time_sheet.cpp
 */

#include "time_sheet.h"

#include <sstream>
#include <libcamera/libcamera.h>

#include "libcamera/internal/formats.h"
#include "libcamera/internal/mapped_framebuffer.h"

using namespace libcamera;

namespace {

double calculateMeanBrightnessFromCenterSpot(libcamera::Request *request)
{
	const Request::BufferMap &buffers = request->buffers();
	for (const auto &[stream, buffer] : buffers) {
		MappedFrameBuffer in(buffer, MappedFrameBuffer::MapFlag::Read);
		if (!in.isValid())
			continue;

		const uint8_t *data = in.planes()[0].data();
		const auto &streamConfig = stream->configuration();
		const auto &formatInfo = PixelFormatInfo::info(streamConfig.pixelFormat);
		double (*calcPixelMean)(const uint8_t *);
		int pixelStride;

		switch (streamConfig.pixelFormat) {
		case formats::NV12:
			calcPixelMean = [](const uint8_t *d) -> double {
				return static_cast<double>(*d);
			};
			pixelStride = 1;
			break;
		case formats::SRGGB10:
			calcPixelMean = [](const uint8_t *d) -> double {
				return static_cast<double>(*reinterpret_cast<const uint16_t *>(d));
			};
			pixelStride = 2;
			break;
		default:
			std::stringstream s;
			s << "Unsupported Pixelformat " << formatInfo.name;
			throw std::invalid_argument(s.str());
		}

		double sum = 0;
		int w = 20;
		int xs = streamConfig.size.width / 2 - w / 2;
		int ys = streamConfig.size.height / 2 - w / 2;

		for (auto y = ys; y < ys + w; y++) {
			auto line = data + y * streamConfig.stride;
			for (auto x = xs; x < xs + w; x++)
				sum += calcPixelMean(line + x * pixelStride);
		}
		sum = sum / (w * w);
		return sum;
	}

	return 0;
}

} /* namespace */

TimeSheetEntry::TimeSheetEntry(const ControlIdMap &idmap)
	: controls_(idmap)
{
}

void TimeSheetEntry::handleCompleteRequest(libcamera::Request *request,
					   const TimeSheetEntry *previous)
{
	metadata_ = request->metadata();

	spotBrightness_ = calculateMeanBrightnessFromCenterSpot(request);
	if (previous)
		brightnessChange_ = spotBrightness_ / previous->spotBrightness();

	sequence_ = request->sequence();
}

std::ostream &operator<<(std::ostream &os, const TimeSheetEntry &te)
{
	os << "=== Frame " << te.sequence_ << std::endl;
	if (!te.controls_.empty()) {
		os << "Controls:" << std::endl;
		const auto &idMap = te.controls_.idMap();
		assert(idMap);
		for (const auto &[id, value] : te.controls_)
			os << "  " << idMap->at(id)->name() << " : "
			   << value.toString() << std::endl;
	}

	if (!te.metadata_.empty()) {
		os << "Metadata:" << std::endl;
		const auto &idMap = te.metadata_.idMap();
		assert(idMap);
		for (const auto &[id, value] : te.metadata_)
			os << "  " << idMap->at(id)->name() << " : "
			   << value.toString() << std::endl;
	}

	os << "Calculated Brightness: " << te.spotBrightness() << std::endl;
	return os;
}

TimeSheetEntry &TimeSheet::get(size_t pos)
{
	entries_.reserve(pos + 1);
	auto &entry = entries_[pos];
	if (!entry)
		entry = std::make_unique<TimeSheetEntry>(idmap_);

	return *entry;
}

void TimeSheet::prepareForQueue(libcamera::Request *request, uint32_t sequence)
{
	request->controls() = get(sequence).controls();
}

void TimeSheet::handleCompleteRequest(libcamera::Request *request)
{
	uint32_t sequence = request->sequence();
	auto &entry = get(sequence);
	TimeSheetEntry *previous = nullptr;
	if (sequence >= 1)
		previous = entries_[sequence - 1].get();

	entry.handleCompleteRequest(request, previous);
}

std::ostream &operator<<(std::ostream &os, const TimeSheet &ts)
{
	for (const auto &entry : ts.entries_) {
		if (entry)
			os << *entry;
	}

	return os;
}
