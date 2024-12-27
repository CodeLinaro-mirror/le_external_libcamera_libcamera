/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2024, Amlogic Inc.
 *
 * c3-isp.cpp - Amlogic Image Processing Algorithms
 */

#include <algorithm>
#include <array>
#include <chrono>
#include <stdint.h>
#include <string.h>

#include <linux/c3-isp-config.h>
#include <linux/v4l2-controls.h>

#include <libcamera/base/file.h>
#include <libcamera/base/log.h>

#include <libcamera/control_ids.h>
#include <libcamera/controls.h>
#include <libcamera/framebuffer.h>
#include <libcamera/request.h>

#include <libcamera/ipa/c3isp_ipa_interface.h>
#include <libcamera/ipa/ipa_interface.h>
#include <libcamera/ipa/ipa_module_info.h>

#include "libcamera/internal/formats.h"
#include "libcamera/internal/mapped_framebuffer.h"
#include "libcamera/internal/yaml_parser.h"

#include "algorithms/algorithm.h"

#include "ipa_context.h"
#include "params.h"

namespace libcamera {

LOG_DEFINE_CATEGORY(IPAC3ISP)

using namespace std::literals::chrono_literals;

namespace ipa::c3isp {

static constexpr uint32_t kMaxFrameContexts = 16;

class IPAC3ISP : public IPAC3ISPInterface, public Module
{
public:
	IPAC3ISP();

	int init(const IPASettings &settings, unsigned int hwRevision,
		 const IPACameraSensorInfo &sensorInfo,
		 const ControlInfoMap &sensorControls,
		 ControlInfoMap *ipaControls) override;
	int start() override;
	void stop() override;

	int configure(const IPAConfigInfo &ipaConfig,
		      ControlInfoMap *ipaControls) override;
	void mapBuffers(const std::vector<IPABuffer> &buffers) override;
	void unmapBuffers(const std::vector<unsigned int> &ids) override;

	void queueRequest(const uint32_t frame, const ControlList &controls) override;
	void fillParamsBuffer(const uint32_t frame, const uint32_t bufferId) override;
	void processStatsBuffer(const uint32_t frame, const uint32_t bufferId,
				const ControlList &sensorControls) override;

protected:
	std::string logPrefix() const override;

private:
	void updateControls(const IPACameraSensorInfo &sensorInfo,
			    const ControlInfoMap &sensorControls,
			    ControlInfoMap *ipaControls);
	void setControls(unsigned int frame);

	std::map<unsigned int, FrameBuffer> buffers_;
	std::map<unsigned int, MappedFrameBuffer> mappedBuffers_;

	ControlInfoMap sensorControls_;

	struct IPAContext context_;
};

namespace {

const ControlInfoMap::Map c3ispControls{
	{ &controls::AwbEnable, ControlInfo(false, true) },
	{ &controls::ColourGains, ControlInfo(0.0f, 3.996f, 1.0f) },
};

} /* namespace */

IPAC3ISP::IPAC3ISP()
	: context_({ {}, {}, { kMaxFrameContexts }, {}, {} })
{
}

std::string IPAC3ISP::logPrefix() const
{
	return "c3isp";
}

int IPAC3ISP::init(const IPASettings &settings, unsigned int hwRevision,
		   const IPACameraSensorInfo &sensorInfo,
		   const ControlInfoMap &sensorControls,
		   ControlInfoMap *ipaControls)
{
	LOG(IPAC3ISP, Debug) << "Hardware revision is " << hwRevision;

	context_.camHelper = CameraSensorHelperFactoryBase::create(settings.sensorModel);
	if (!context_.camHelper) {
		LOG(IPAC3ISP, Error)
			<< "Failed to create camera sensor helper for "
			<< settings.sensorModel;
		return -ENODEV;
	}

	context_.configuration.sensor.lineDuration =
		sensorInfo.minLineLength * 1.0s / sensorInfo.pixelRate;

	File file(settings.configurationFile);
	if (!file.open(File::OpenModeFlag::ReadOnly)) {
		int ret = file.error();
		LOG(IPAC3ISP, Error)
			<< "Failed to open configuration file "
			<< settings.configurationFile << ": " << strerror(-ret);
		return ret;
	}

	std::unique_ptr<libcamera::YamlObject> data = YamlParser::parse(file);
	if (!data)
		return -EINVAL;

	unsigned int version = (*data)["version"].get<uint32_t>(0);
	if (version != 1) {
		LOG(IPAC3ISP, Error)
			<< "Invalid tuning file version " << version;
		return -EINVAL;
	}

	if (!data->contains("algorithms")) {
		LOG(IPAC3ISP, Error)
			<< "Tuning file doesn't contain any algorithm";
		return -EINVAL;
	}

	int ret = createAlgorithms(context_, (*data)["algorithms"]);
	if (ret)
		return ret;

	updateControls(sensorInfo, sensorControls, ipaControls);

	return 0;
}

int IPAC3ISP::start()
{
	setControls(0);

	return 0;
}

void IPAC3ISP::stop()
{
	context_.frameContexts.clear();
}

int IPAC3ISP::configure(const IPAConfigInfo &ipaConfig,
			ControlInfoMap *ipaControls)
{
	sensorControls_ = ipaConfig.sensorControls;

	const auto itExp = sensorControls_.find(V4L2_CID_EXPOSURE);
	int32_t minExposure = itExp->second.min().get<int32_t>();
	int32_t maxExposure = itExp->second.max().get<int32_t>();

	const auto itGain = sensorControls_.find(V4L2_CID_ANALOGUE_GAIN);
	int32_t minGain = itGain->second.min().get<int32_t>();
	int32_t maxGain = itGain->second.max().get<int32_t>();

	LOG(IPAC3ISP, Debug)
		<< "Exposure: [" << minExposure << ", " << maxExposure
		<< "], gain: [" << minGain << ", " << maxGain << "]";

	context_.configuration = {};
	context_.activeState = {};
	context_.frameContexts.clear();

	const IPACameraSensorInfo &info = ipaConfig.sensorInfo;

	const ControlInfo vBlank = sensorControls_.find(V4L2_CID_VBLANK)->second;
	context_.configuration.sensor.defVBlank = vBlank.def().get<int32_t>();
	context_.configuration.sensor.size = info.outputSize;
	context_.configuration.sensor.lineDuration = info.minLineLength * 1.0s / info.pixelRate;

	updateControls(info, sensorControls_, ipaControls);

	context_.configuration.sensor.minShutterSpeed =
		minExposure * context_.configuration.sensor.lineDuration;
	context_.configuration.sensor.maxShutterSpeed =
		maxExposure * context_.configuration.sensor.lineDuration;
	context_.configuration.sensor.minAnalogueGain =
		context_.camHelper->gain(minGain);
	context_.configuration.sensor.maxAnalogueGain =
		context_.camHelper->gain(maxGain);

	for (auto const &a : algorithms()) {
		Algorithm *algo = static_cast<Algorithm *>(a.get());

		if (algo->disabled_)
			continue;

		int ret = algo->configure(context_, info);
		if (ret)
			return ret;
	}

	return 0;
}

void IPAC3ISP::mapBuffers(const std::vector<IPABuffer> &buffers)
{
	for (const IPABuffer &buffer : buffers) {
		auto elem = buffers_.emplace(std::piecewise_construct,
					     std::forward_as_tuple(buffer.id),
					     std::forward_as_tuple(buffer.planes));
		const FrameBuffer &fb = elem.first->second;

		MappedFrameBuffer mappedBuffer(&fb, MappedFrameBuffer::MapFlag::ReadWrite);
		if (!mappedBuffer.isValid()) {
			LOG(IPAC3ISP, Fatal) << "Failed tommap buffer: "
					     << strerror(mappedBuffer.error());
		}

		mappedBuffers_.emplace(buffer.id, std::move(mappedBuffer));
	}
}

void IPAC3ISP::unmapBuffers(const std::vector<unsigned int> &ids)
{
	for (unsigned int id : ids) {
		const auto fb = buffers_.find(id);
		if (fb == buffers_.end())
			continue;

		mappedBuffers_.erase(id);
		buffers_.erase(id);
	}
}

void IPAC3ISP::queueRequest(const uint32_t frame, const ControlList &controls)
{
	IPAFrameContext &frameContext = context_.frameContexts.alloc(frame);

	for (auto const &a : algorithms()) {
		Algorithm *algo = static_cast<Algorithm *>(a.get());
		if (algo->disabled_)
			continue;
		algo->queueRequest(context_, frame, frameContext, controls);
	}
}

void IPAC3ISP::fillParamsBuffer(const uint32_t frame, const uint32_t bufferId)
{
	IPAFrameContext &frameContext = context_.frameContexts.get(frame);

	C3ISPParams params(mappedBuffers_.at(bufferId).planes()[0]);

	for (auto const &algo : algorithms())
		algo->prepare(context_, frame, frameContext, &params);

	paramsBufferReady.emit(frame, params.size());
}

void IPAC3ISP::processStatsBuffer(const uint32_t frame, const uint32_t bufferId,
				  const ControlList &sensorControls)
{
	IPAFrameContext &frameContext = context_.frameContexts.get(frame);

	const c3_isp_stats_info *stats = nullptr;

	stats = reinterpret_cast<c3_isp_stats_info *>(
		mappedBuffers_.at(bufferId).planes()[0].data());

	frameContext.sensor.exposure =
		sensorControls.get(V4L2_CID_EXPOSURE).get<int32_t>();
	frameContext.sensor.gain =
		context_.camHelper->gain(sensorControls.get(V4L2_CID_ANALOGUE_GAIN).get<int32_t>());

	ControlList metadata(controls::controls);

	for (auto const &a : algorithms()) {
		Algorithm *algo = static_cast<Algorithm *>(a.get());
		if (algo->disabled_)
			continue;
		algo->process(context_, frame, frameContext, stats, metadata);
	}

	setControls(frame);

	metadataReady.emit(frame, metadata);
}

void IPAC3ISP::updateControls(const IPACameraSensorInfo &sensorInfo,
			      const ControlInfoMap &sensorControls,
			      ControlInfoMap *ipaControls)
{
	ControlInfoMap::Map ctrlMap = c3ispControls;

	double lineDuration = context_.configuration.sensor.lineDuration.get<std::micro>();
	const ControlInfo &v4l2Exposure = sensorControls.find(V4L2_CID_EXPOSURE)->second;
	int32_t minExposure = v4l2Exposure.min().get<int32_t>() * lineDuration;
	int32_t maxExposure = v4l2Exposure.max().get<int32_t>() * lineDuration;
	int32_t defExposure = v4l2Exposure.def().get<int32_t>() * lineDuration;
	ctrlMap.emplace(std::piecewise_construct,
			std::forward_as_tuple(&controls::ExposureTime),
			std::forward_as_tuple(minExposure, maxExposure, defExposure));

	const ControlInfo &v4l2Gain = sensorControls.find(V4L2_CID_ANALOGUE_GAIN)->second;
	float minGain = context_.camHelper->gain(v4l2Gain.min().get<int32_t>());
	float maxGain = context_.camHelper->gain(v4l2Gain.max().get<int32_t>());
	float defGain = context_.camHelper->gain(v4l2Gain.def().get<int32_t>());
	ctrlMap.emplace(std::piecewise_construct,
			std::forward_as_tuple(&controls::AnalogueGain),
			std::forward_as_tuple(minGain, maxGain, defGain));

	const ControlInfo &v4l2HBlank = sensorControls.find(V4L2_CID_HBLANK)->second;
	uint32_t hblank = v4l2HBlank.def().get<int32_t>();
	uint32_t lineLength = sensorInfo.outputSize.width + hblank;

	const ControlInfo &v4l2VBlank = sensorControls.find(V4L2_CID_VBLANK)->second;
	std::array<uint32_t, 3> frameHeights{
		v4l2VBlank.min().get<int32_t>() + sensorInfo.outputSize.height,
		v4l2VBlank.max().get<int32_t>() + sensorInfo.outputSize.height,
		v4l2VBlank.def().get<int32_t>() + sensorInfo.outputSize.height,
	};

	std::array<int64_t, 3> frameDurations;
	for (unsigned int i = 0; i < frameHeights.size(); ++i) {
		uint64_t frameSize = lineLength * frameHeights[i];
		frameDurations[i] = frameSize / (sensorInfo.pixelRate / 1000000U);
	}

	ctrlMap[&controls::FrameDurationLimits] = ControlInfo(frameDurations[0],
							      frameDurations[1],
							      frameDurations[2]);
	ctrlMap.insert(context_.ctrlMap.begin(), context_.ctrlMap.end());
	*ipaControls = ControlInfoMap(std::move(ctrlMap), controls::controls);
}

void IPAC3ISP::setControls(unsigned int frame)
{
	uint32_t exposure = context_.activeState.agc.exposure;
	uint32_t gain = context_.camHelper->gainCode(context_.activeState.agc.gain);

	ControlList ctrls(sensorControls_);
	ctrls.set(V4L2_CID_EXPOSURE, static_cast<int32_t>(exposure));
	ctrls.set(V4L2_CID_ANALOGUE_GAIN, static_cast<int32_t>(gain));

	setSensorControls.emit(frame, ctrls);
}

} /* namespace ipa::c3isp */

/*
 * External IPA module interface
 */

extern "C" {
const struct IPAModuleInfo ipaModuleInfo = {
	IPA_MODULE_API_VERSION,
	1,
	"c3isp",
	"c3isp",
};

IPAInterface *ipaCreate()
{
	return new ipa::c3isp::IPAC3ISP();
}
}

} /* namespace libcamera */
