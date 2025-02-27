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

#include <libcamera/ipa/c3-isp_ipa_interface.h>
#include <libcamera/ipa/ipa_interface.h>
#include <libcamera/ipa/ipa_module_info.h>

#include "libcamera/internal/formats.h"
#include "libcamera/internal/mapped_framebuffer.h"
#include "libcamera/internal/yaml_parser.h"

#include "algorithms/algorithm.h"
#include "libipa/camera_sensor_helper.h"

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

	int init(const IPASettings &settings, const IPAConfigInfo &ipaConfig,
		 ControlInfoMap *ipaControls) override;
	int start() override;
	void stop() override;

	int configure(const IPAConfigInfo &ipaConfig,
		      ControlInfoMap *ipaControls) override;
	void mapBuffers(const std::vector<IPABuffer> &buffers, bool readOnly) override;
	void unmapBuffers(const std::vector<IPABuffer> &buffers) override;

	void queueRequest(const uint32_t request, const ControlList &controls) override;
	void computeParams(const uint32_t request, const uint32_t bufferId) override;
	void processStats(const uint32_t request, const uint32_t bufferId,
			  const ControlList &sensorControls) override;

protected:
	std::string logPrefix() const override;

private:
	void updateSessionConfiguration(const IPACameraSensorInfo &info,
					const ControlInfoMap &sensorControls);
	void updateControls(const IPACameraSensorInfo &sensorInfo,
			    const ControlInfoMap &sensorControls,
			    ControlInfoMap *ipaControls);
	void setControls();

	std::map<unsigned int, MappedFrameBuffer> buffers_;

	ControlInfoMap sensorControls_;

	/* Interface to the Camera Helper */
	std::unique_ptr<CameraSensorHelper> camHelper_;

	/* Local parameter storage */
	struct IPAContext context_;
};

IPAC3ISP::IPAC3ISP()
	: context_(kMaxFrameContexts)
{
}

std::string IPAC3ISP::logPrefix() const
{
	return "c3isp";
}

int IPAC3ISP::init(const IPASettings &settings, const IPAConfigInfo &ipaConfig,
		   ControlInfoMap *ipaControls)
{
	camHelper_ = CameraSensorHelperFactoryBase::create(settings.sensorModel);
	if (!camHelper_) {
		LOG(IPAC3ISP, Error)
			<< "Failed to create camera sensor helper for "
			<< settings.sensorModel;
		return -ENODEV;
	}

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

	if (!data->contains("algorithms")) {
		LOG(IPAC3ISP, Error)
			<< "Tuning file doesn't contain any algorithm";
		return -EINVAL;
	}

	int ret = createAlgorithms(context_, (*data)["algorithms"]);
	if (ret)
		return ret;

	updateControls(ipaConfig.sensorInfo, ipaConfig.sensorControls, ipaControls);

	return 0;
}

int IPAC3ISP::start()
{
	return 0;
}

void IPAC3ISP::stop()
{
	context_.frameContexts.clear();
}

void IPAC3ISP::updateSessionConfiguration(const IPACameraSensorInfo &info,
					  const ControlInfoMap &sensorControls)
{
	const ControlInfo &v4l2Exposure = sensorControls.find(V4L2_CID_EXPOSURE)->second;
	int32_t minExposure = v4l2Exposure.min().get<int32_t>();
	int32_t maxExposure = v4l2Exposure.max().get<int32_t>();
	int32_t defExposure = v4l2Exposure.def().get<int32_t>();

	const ControlInfo &v4l2Gain = sensorControls.find(V4L2_CID_ANALOGUE_GAIN)->second;
	int32_t minGain = v4l2Gain.min().get<int32_t>();
	int32_t maxGain = v4l2Gain.max().get<int32_t>();

	context_.configuration.sensor.lineDuration = info.minLineLength * 1.0s / info.pixelRate;
	context_.configuration.agc.minShutterSpeed = minExposure * context_.configuration.sensor.lineDuration;
	context_.configuration.agc.maxShutterSpeed = maxExposure * context_.configuration.sensor.lineDuration;
	context_.configuration.agc.defaultExposure = defExposure;
	context_.configuration.agc.minAnalogueGain = camHelper_->gain(minGain);
	context_.configuration.agc.maxAnalogueGain = camHelper_->gain(maxGain);

	context_.configuration.sensor.size = info.outputSize;

	if (camHelper_->blackLevel().has_value())
		/*
		 * The black level from CameraSensorHelper is a 16-bit value.
		 * The C3 ISP expects 20-bit settings, so we shift it to the
		 * appropriate width
		 */
		context_.configuration.sensor.blackLevel = camHelper_->blackLevel().value() << 4;
}

void IPAC3ISP::updateControls(const IPACameraSensorInfo &sensorInfo,
			      const ControlInfoMap &sensorControls,
			      ControlInfoMap *ipaControls)
{
	ControlInfoMap::Map ctrlMap;

	/* Compute the frame duration limits. */
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

	/* Compute the exposure time limits */
	double lineDuration = context_.configuration.sensor.lineDuration.get<std::micro>();
	const ControlInfo &v4l2Exposure = sensorControls.find(V4L2_CID_EXPOSURE)->second;
	int32_t minExposure = v4l2Exposure.min().get<int32_t>() * lineDuration;
	int32_t maxExposure = v4l2Exposure.max().get<int32_t>() * lineDuration;
	int32_t defExposure = v4l2Exposure.def().get<int32_t>() * lineDuration;
	ctrlMap[&controls::ExposureTime] = ControlInfo(minExposure, maxExposure, defExposure);

	/* Compute the analogue gain limits. */
	const ControlInfo &v4l2Gain = sensorControls.find(V4L2_CID_ANALOGUE_GAIN)->second;
	float minGain = camHelper_->gain(v4l2Gain.min().get<int32_t>());
	float maxGain = camHelper_->gain(v4l2Gain.max().get<int32_t>());
	float defGain = camHelper_->gain(v4l2Gain.def().get<int32_t>());
	ctrlMap[&controls::AnalogueGain] = ControlInfo(minGain, maxGain, defGain);

	/* Merge the cotrols */
	ctrlMap.merge(context_.ctrlMap);

	*ipaControls = ControlInfoMap(std::move(ctrlMap), controls::controls);
}

int IPAC3ISP::configure(const IPAConfigInfo &ipaConfig,
			ControlInfoMap *ipaControls)
{
	sensorControls_ = ipaConfig.sensorControls;

	context_.configuration = {};
	context_.activeState = {};
	context_.frameContexts.clear();

	const IPACameraSensorInfo &info = ipaConfig.sensorInfo;

	updateSessionConfiguration(info, ipaConfig.sensorControls);

	updateControls(info, ipaConfig.sensorControls, ipaControls);

	for (auto const &a : algorithms()) {
		Algorithm *algo = static_cast<Algorithm *>(a.get());

		int ret = algo->configure(context_, info);
		if (ret)
			return ret;
	}

	return 0;
}

void IPAC3ISP::mapBuffers(const std::vector<IPABuffer> &buffers, bool readOnly)
{
	for (const IPABuffer &buffer : buffers) {
		const FrameBuffer fb(buffer.planes);
		buffers_.emplace(
			buffer.id,
			MappedFrameBuffer(
				&fb,
				readOnly ? MappedFrameBuffer::MapFlag::Read
					 : MappedFrameBuffer::MapFlag::ReadWrite));
	}
}

void IPAC3ISP::unmapBuffers(const std::vector<IPABuffer> &buffers)
{
	for (const IPABuffer &buffer : buffers) {
		auto it = buffers_.find(buffer.id);
		if (it == buffers_.end())
			continue;

		buffers_.erase(buffer.id);
	}
}

void IPAC3ISP::queueRequest(const uint32_t request, const ControlList &controls)
{
	IPAFrameContext &frameContext = context_.frameContexts.alloc(request);

	for (auto const &a : algorithms()) {
		Algorithm *algo = static_cast<Algorithm *>(a.get());

		algo->queueRequest(context_, request, frameContext, controls);
	}
}

void IPAC3ISP::computeParams(const uint32_t request, const uint32_t bufferId)
{
	IPAFrameContext &frameContext = context_.frameContexts.get(request);

	C3ISPParams params(buffers_.at(bufferId).planes()[0]);

	for (auto const &algo : algorithms())
		algo->prepare(context_, request, frameContext, &params);

	paramsComputed.emit(request, params.size());
}

void IPAC3ISP::processStats(const uint32_t request, const uint32_t bufferId,
			    const ControlList &sensorControls)
{
	IPAFrameContext &frameContext = context_.frameContexts.get(request);
	const c3_isp_stats_info *stats = nullptr;

	stats = reinterpret_cast<c3_isp_stats_info *>(
		buffers_.at(bufferId).planes()[0].data());

	frameContext.agc.exposure =
		sensorControls.get(V4L2_CID_EXPOSURE).get<int32_t>();
	frameContext.agc.sensorGain =
		camHelper_->gain(sensorControls.get(V4L2_CID_ANALOGUE_GAIN).get<int32_t>());

	ControlList metadata(controls::controls);

	for (auto const &a : algorithms()) {
		Algorithm *algo = static_cast<Algorithm *>(a.get());

		algo->process(context_, request, frameContext, stats, metadata);
	}

	setControls();

	statsProcessed.emit(request, metadata);
}

void IPAC3ISP::setControls()
{
	IPAActiveState &activeState = context_.activeState;
	uint32_t exposure;
	uint32_t gain;

	if (activeState.agc.autoEnabled) {
		exposure = activeState.agc.automatic.exposure;
		gain = camHelper_->gainCode(activeState.agc.automatic.sensorGain);
	} else {
		exposure = activeState.agc.manual.exposure;
		gain = camHelper_->gainCode(activeState.agc.manual.sensorGain);
	}

	ControlList ctrls(sensorControls_);
	ctrls.set(V4L2_CID_EXPOSURE, static_cast<int32_t>(exposure));
	ctrls.set(V4L2_CID_ANALOGUE_GAIN, static_cast<int32_t>(gain));

	setSensorControls.emit(ctrls);
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
