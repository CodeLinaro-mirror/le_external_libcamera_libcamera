/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2026 Renesas Electronics Corp.
 * Copyright (C) 2026 Ideas on Board Oy
 * Copyright (C) 2026 Ragnatech AB
 *
 * RPP-X1 Image Processing Algorithms
 */

#include <algorithm>
#include <stdint.h>
#include <string.h>

#include <libcamera/base/file.h>
#include <libcamera/base/log.h>

#include <libcamera/controls.h>
#include <libcamera/framebuffer.h>

#include <libcamera/ipa/ipa_interface.h>
#include <libcamera/ipa/ipa_module_info.h>
#include <libcamera/ipa/rppx1_ipa_interface.h>

#include "libcamera/internal/formats.h"
#include "libcamera/internal/mapped_framebuffer.h"
#include "libcamera/internal/yaml_parser.h"

#include "algorithms/algorithm.h"

#include "ipa_context.h"
#include "params.h"
#include "stats.h"

namespace libcamera {

LOG_DEFINE_CATEGORY(IPARppX1)

namespace ipa::rppx1 {

/* Maximum number of frame contexts to be held */
static constexpr uint32_t kMaxFrameContexts = 16;

class IPARppX1 : public IPARppX1Interface, public Module
{
public:
	IPARppX1();

	int init(const IPASettings &settings,
		 const IPACameraSensorInfo &sensorInfo,
		 const ControlInfoMap &sensorControls,
		 ControlInfoMap *ipaControls) override;
	int start() override;
	void stop() override;

	int configure(const IPAConfigInfo &ipaConfig,
		      const std::map<uint32_t, IPAStream> &streamConfig,
		      ControlInfoMap *ipaControls) override;
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

	std::map<unsigned int, FrameBuffer> buffers_;
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

void IPARppX1::updateControls(ControlInfoMap *ipaControls)
{
	ControlInfoMap::Map ctrlMap;

	ctrlMap.insert(context_.ctrlMap.begin(), context_.ctrlMap.end());
	*ipaControls = ControlInfoMap(std::move(ctrlMap), controls::controls);
}

int IPARppX1::init(const IPASettings &settings,
		   const IPACameraSensorInfo &sensorInfo,
		   [[maybe_unused]] const ControlInfoMap &sensorControls,
		   [[maybe_unused]] ControlInfoMap *ipaControls)
{
	context_.sensorInfo = sensorInfo;

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

	std::unique_ptr<libcamera::ValueNode> data = YamlParser::parse(file);
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

int IPARppX1::configure(const IPAConfigInfo &ipaConfig,
			[[maybe_unused]] const std::map<uint32_t, IPAStream> &streamConfig,
			[[maybe_unused]] ControlInfoMap *ipaControls)
{
	/* Clear the IPA context before the streaming session. */
	context_.configuration = {};
	context_.activeState = {};
	context_.frameContexts.clear();

	const IPACameraSensorInfo &info = ipaConfig.sensorInfo;

	/* Update the camera controls using the new sensor settings. */
	updateControls(ipaControls);

	for (auto const &a : algorithms()) {
		Algorithm *algo = static_cast<Algorithm *>(a.get());

		int ret = algo->configure(context_, info);
		if (ret)
			return ret;
	}

	return 0;
}

void IPARppX1::mapBuffers(const std::vector<IPABuffer> &buffers)
{
	for (const IPABuffer &buffer : buffers) {
		auto elem = buffers_.emplace(std::piecewise_construct,
					     std::forward_as_tuple(buffer.id),
					     std::forward_as_tuple(buffer.planes));
		const FrameBuffer &fb = elem.first->second;

		MappedFrameBuffer mappedBuffer(&fb, MappedFrameBuffer::MapFlag::ReadWrite);
		if (!mappedBuffer.isValid()) {
			LOG(IPARppX1, Fatal) << "Failed to mmap buffer: "
					     << strerror(mappedBuffer.error());
		}

		mappedBuffers_.emplace(buffer.id, std::move(mappedBuffer));
	}
}

void IPARppX1::unmapBuffers(const std::vector<unsigned int> &ids)
{
	for (unsigned int id : ids) {
		const auto fb = buffers_.find(id);
		if (fb == buffers_.end())
			continue;

		mappedBuffers_.erase(id);
		buffers_.erase(id);
	}
}

void IPARppX1::queueRequest(const uint32_t frame, const ControlList &controls)
{
	IPAFrameContext &frameContext = context_.frameContexts.alloc(frame);

	for (auto const &a : algorithms()) {
		Algorithm *algo = static_cast<Algorithm *>(a.get());
		if (algo->disabled_)
			continue;

		algo->queueRequest(context_, frame, frameContext, controls);
	}
}

void IPARppX1::computeParams(const uint32_t frame, const uint32_t bufferId)
{
	IPAFrameContext &frameContext = context_.frameContexts.get(frame);

	RppX1Params params(mappedBuffers_.at(bufferId).planes()[0]);

	for (auto const &algo : algorithms())
		algo->prepare(context_, frame, frameContext, &params);

	paramsComputed.emit(frame, params.bytesused());
}

void IPARppX1::processStats(const uint32_t frame, const uint32_t bufferId,
			    [[maybe_unused]] const ControlList &sensorControls)
{
	IPAFrameContext &frameContext = context_.frameContexts.get(frame);

	ControlList metadata(controls::controls);

	auto stats = RppX1Stats(mappedBuffers_.at(bufferId).planes()[0]);
	if (!stats)
		return;

	for (auto const &a : algorithms()) {
		Algorithm *algo = static_cast<Algorithm *>(a.get());
		if (algo->disabled_)
			continue;
		algo->process(context_, frame, frameContext, &stats, metadata);
	}

	metadataReady.emit(frame, metadata);
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
