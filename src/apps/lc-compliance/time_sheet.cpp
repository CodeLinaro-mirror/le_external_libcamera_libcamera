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

double calcPixelMeanNV12(const uint8_t *data)
{
	return (double)*data;
}

double calcPixelMeanRAW10(const uint8_t *data)
{
	return (double)*((const uint16_t *)data);
}

double calculateMeanBrightnessFromCenterSpot(libcamera::Request *request)
{
	const Request::BufferMap &buffers = request->buffers();
	for (const auto &[stream, buffer] : buffers) {
		MappedFrameBuffer in(buffer, MappedFrameBuffer::MapFlag::Read);
		if (in.isValid()) {
			auto data = in.planes()[0].data();
			auto streamConfig = stream->configuration();
			auto formatInfo = PixelFormatInfo::info(streamConfig.pixelFormat);

			std::function<double(const uint8_t *data)> calcPixelMean;
			int pixelStride;

			switch (streamConfig.pixelFormat) {
			case formats::NV12:
				calcPixelMean = calcPixelMeanNV12;
				pixelStride = 1;
				break;
			case formats::SRGGB10:
				calcPixelMean = calcPixelMeanRAW10;
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
				for (auto x = xs; x < xs + w; x++) {
					sum += calcPixelMean(line + x * pixelStride);
				}
			}
			sum = sum / (w * w);
			return sum;
		}
	}
	return 0;
}

TimeSheetEntry::TimeSheetEntry(const ControlIdMap &idmap)
	: controls_(idmap)
{
}

void TimeSheetEntry::handleCompleteRequest(libcamera::Request *request, const TimeSheetEntry *previous)
{
	metadata_ = request->metadata();

	spotBrightness_ = calculateMeanBrightnessFromCenterSpot(request);
	if (previous) {
		brightnessChange_ = spotBrightness_ / previous->getSpotBrightness();
	}
	sequence_ = request->sequence();
}

void TimeSheetEntry::printInfo()
{
	std::cout << "=== Frame " << sequence_ << std::endl;
	if (!controls_.empty()) {
		std::cout << "Controls:" << std::endl;
		auto idMap = controls_.idMap();
		assert(idMap);
		for (const auto &[id, value] : controls_) {
			std::cout << "  " << idMap->at(id)->name() << " : " << value.toString() << std::endl;
		}
	}

	if (!metadata_.empty()) {
		std::cout << "Metadata:" << std::endl;
		auto idMap = metadata_.idMap();
		assert(idMap);
		for (const auto &[id, value] : metadata_) {
			std::cout << "  " << idMap->at(id)->name() << " : " << value.toString() << std::endl;
		}
	}

	std::cout << "Calculated Brightness: " << spotBrightness_ << std::endl;
}

TimeSheetEntry &TimeSheet::get(size_t pos)
{
	auto &entry = entries_[pos];
	if (!entry)
		entry = std::make_shared<TimeSheetEntry>(idmap_);
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
	if (sequence >= 1) {
		previous = entries_[sequence - 1].get();
	}

	entry.handleCompleteRequest(request, previous);
}

void TimeSheet::printAllInfos()
{
	for (auto entry : entries_) {
		if (entry)
			entry->printInfo();
	}
}
