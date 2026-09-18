/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2019, Google Inc.
 *
 * RPP-X1 Image Processing Algorithms
 */

#include <map>
#include <stdint.h>
#include <string.h>
#include <utility>

#include <linux/v4l2-controls.h>

#include <libcamera/base/file.h>
#include <libcamera/base/log.h>

#include <libcamera/control_ids.h>
#include <libcamera/controls.h>
#include <libcamera/framebuffer.h>
#include <libcamera/request.h>

#include <libcamera/ipa/core_ipa_interface.h>
#include <libcamera/ipa/ipa_module_info.h>
#include <libcamera/ipa/rppx1_ipa_interface.h>

#include "libcamera/internal/mapped_framebuffer.h"
#include "libcamera/internal/yaml_parser.h"

#include <libipa/agc.h>

#include "algorithms/algorithm.h"

#include "ipa_context.h"
#include "module.h"
#include "params.h"
#include "stats.h"

namespace libcamera {

LOG_DEFINE_CATEGORY(IPARppX1)

namespace ipa::rppx1 {

/* Maximum number of frame contexts to be held */
static constexpr uint32_t kMaxFrameContexts = 16;

class IPARppX1 final : public IPARppX1Interface, public Module
{
public:
	IPARppX1();

	int init(const IPASettings &settings,
		 const IPACameraSensorInfo &sensorInfo,
		 const ControlInfoMap &sensorControls,
		 ControlInfoMap *ipaControls) override;
	int start() override;
	void stop() override;

	int configure(const IPAConfigInfo &ipaConfig, ControlInfoMap *ipaControls) override;
	void mapBuffers(const std::vector<IPABuffer> &buffers) override;
	void unmapBuffers(const std::vector<unsigned int> &ids) override;

	void queueRequest(const uint32_t frame, const ControlList &controls) override;
	void computeParams(const uint32_t frame, const uint32_t bufferId) override;
	void processStats(const uint32_t frame, const uint32_t bufferId,
			  const ControlList &sensorControls) override;

protected:
	std::string logPrefix() const override;

private:
	void updateControls(ControlInfoMap *ipaControls);
	void setControls(unsigned int frame);

	std::map<unsigned int, MappedFrameBuffer> mappedBuffers_;

	/* Local parameter storage */
	struct IPAContext context_;
};

IPARppX1::IPARppX1()
	: context_(kMaxFrameContexts)
{
}

std::string IPARppX1::logPrefix() const
{
	return "rppx1";
}

int IPARppX1::init(const IPASettings &settings,
		   const IPACameraSensorInfo &sensorInfo,
		   const ControlInfoMap &sensorControls,
		   ControlInfoMap *ipaControls)
{
	context_.sensorInfo = sensorInfo;
	context_.sensorControls = sensorControls;

	context_.camHelper = CameraSensorHelperFactoryBase::create(settings.sensorModel);
	if (!context_.camHelper) {
		LOG(IPARppX1, Error)
			<< "Failed to create camera sensor helper for "
			<< settings.sensorModel;
		return -ENODEV;
	}

	/* Load the tuning data file. */
	File file(settings.configurationFile);
	if (!file.open(File::OpenModeFlag::ReadOnly)) {
		int ret = file.error();
		LOG(IPARppX1, Error)
			<< "Failed to open configuration file "
			<< settings.configurationFile << ": " << strerror(-ret);
		return ret;
	}

	std::unique_ptr<ValueNode> data = YamlParser::parse(file);
	if (!data)
		return -EINVAL;

	unsigned int version = (*data)["version"].get<uint32_t>(0);
	if (version != 1) {
		LOG(IPARppX1, Error)
			<< "Invalid tuning file version " << version;
		return -EINVAL;
	}

	if (!data->contains("algorithms")) {
		LOG(IPARppX1, Error)
			<< "Tuning file doesn't contain any algorithm";
		return -EINVAL;
	}

	int ret = createAlgorithms(context_, (*data)["algorithms"]);
	if (ret)
		return ret;

	/* Initialize controls. */
	updateControls(ipaControls);

	return 0;
}

int IPARppX1::start()
{
	/* \todo Properly handle startup controls. */
	return 0;
}

void IPARppX1::stop()
{
	context_.frameContexts.clear();
}

int IPARppX1::configure(const IPAConfigInfo &ipaConfig, ControlInfoMap *ipaControls)
{
	context_.sensorInfo = ipaConfig.sensorInfo;
	context_.sensorControls = ipaConfig.sensorControls;

	/* Clear the IPA context before the streaming session. */
	context_.configuration = {};
	context_.activeState = {};
	context_.frameContexts.clear();

	for (const auto &a : algorithms()) {
		Algorithm *algo = static_cast<Algorithm *>(a.get());

		int ret = algo->configure(context_, context_.sensorInfo);
		if (ret)
			return ret;
	}

	updateControls(ipaControls);

	return 0;
}

void IPARppX1::mapBuffers(const std::vector<IPABuffer> &buffers)
{
	for (const IPABuffer &buffer : buffers) {
		FrameBuffer fb(buffer.planes);

		auto [it, inserted] = mappedBuffers_.try_emplace(
			buffer.id, &fb, MappedFrameBuffer::MapFlag::ReadWrite);
		ASSERT(inserted);

		if (!it->second.isValid()) {
			LOG(IPARppX1, Fatal)
				<< "Failed to mmap buffer: "
				<< strerror(it->second.error());
		}
	}
}

void IPARppX1::unmapBuffers(const std::vector<unsigned int> &ids)
{
	for (unsigned int id : ids)
		mappedBuffers_.erase(id);
}

void IPARppX1::queueRequest(const uint32_t frame, const ControlList &controls)
{
	IPAFrameContext &frameContext = context_.frameContexts.alloc(frame);

	for (const auto &a : algorithms()) {
		Algorithm *algo = static_cast<Algorithm *>(a.get());
		algo->queueRequest(context_, frame, frameContext, controls);
	}
}

void IPARppX1::computeParams(const uint32_t frame, const uint32_t bufferId)
{
	IPAFrameContext &frameContext = context_.frameContexts.get(frame);

	RppX1Params params(mappedBuffers_.at(bufferId).planes()[0]);

	for (const auto &algo : algorithms())
		algo->prepare(context_, frame, frameContext, &params);

	paramsComputed.emit(frame, params.bytesused());
}

void IPARppX1::processStats(const uint32_t frame, const uint32_t bufferId,
			    [[maybe_unused]] const ControlList &sensorControls)
{
	auto stats = RppX1Stats(mappedBuffers_.at(bufferId).planes()[0]);
	if (!stats)
		return;

	IPAFrameContext &frameContext = context_.frameContexts.get(frame);
	ControlList metadata(controls::controls);

	std::tie(frameContext.sensor.exposure, frameContext.sensor.gain) =
		agc::extractControls(sensorControls, context_.camHelper.get());

	for (auto const &a : algorithms()) {
		Algorithm *algo = static_cast<Algorithm *>(a.get());
		algo->process(context_, frame, frameContext, &stats, metadata);
	}

	setControls(frame);

	metadataReady.emit(frame, metadata);
}

void IPARppX1::updateControls(ControlInfoMap *ipaControls)
{
	ControlInfoMap::Map ctrlMap = {};

	ctrlMap.insert(context_.ctrlMap.begin(), context_.ctrlMap.end());
	*ipaControls = { std::move(ctrlMap), controls::controls };
}

void IPARppX1::setControls(unsigned int frame)
{
	IPAFrameContext &frameContext = context_.frameContexts.get(frame);

	uint32_t exposure = frameContext.agc.exposure;
	uint32_t vblank = frameContext.agc.vblank;

	LOG(IPARppX1, Debug)
		<< "Set controls for frame " << frame << ": exposure " << exposure
		<< ", gain " << frameContext.agc.gain << ", vblank " << vblank;

	ControlList ctrls(context_.sensorControls);
	agc::prepareControls(ctrls, context_.camHelper.get(),
			     exposure, frameContext.agc.gain);
	ctrls.set(V4L2_CID_VBLANK, static_cast<int32_t>(vblank));

	setSensorControls.emit(frame, ctrls);
}

} /* namespace ipa::rppx1 */

/*
 * External IPA module interface
 */

extern "C" {
const struct IPAModuleInfo ipaModuleInfo = {
	IPA_MODULE_API_VERSION,
	1,
	"rppx1",
};

IPAInterface *ipaCreate()
{
	return new ipa::rppx1::IPARppX1();
}
}

} /* namespace libcamera */
