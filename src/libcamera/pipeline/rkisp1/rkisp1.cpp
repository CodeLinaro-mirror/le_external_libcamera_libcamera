/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2019, Google Inc.
 *
 * Pipeline handler for Rockchip ISP1
 */

#include <algorithm>
#include <deque>
#include <iterator>
#include <map>
#include <memory>
#include <numeric>
#include <optional>
#include <queue>
#include <vector>

#include <linux/media-bus-format.h>
#include <linux/rkisp1-config.h>

#include <libcamera/base/file.h>
#include <libcamera/base/log.h>
#include <libcamera/base/utils.h>

#include <libcamera/camera.h>
#include <libcamera/color_space.h>
#include <libcamera/control_ids.h>
#include <libcamera/formats.h>
#include <libcamera/framebuffer.h>
#include <libcamera/property_ids.h>
#include <libcamera/request.h>
#include <libcamera/stream.h>
#include <libcamera/transform.h>

#include <libcamera/ipa/core_ipa_interface.h>
#include <libcamera/ipa/rkisp1_ipa_interface.h>
#include <libcamera/ipa/rkisp1_ipa_proxy.h>

#include "libcamera/internal/camera.h"
#include "libcamera/internal/camera_sensor.h"
#include "libcamera/internal/camera_sensor_properties.h"
#include "libcamera/internal/converter/converter_dw100.h"
#include "libcamera/internal/delayed_controls.h"
#include "libcamera/internal/device_enumerator.h"
#include "libcamera/internal/framebuffer.h"
#include "libcamera/internal/ipa_manager.h"
#include "libcamera/internal/media_device.h"
#include "libcamera/internal/media_pipeline.h"
#include "libcamera/internal/pipeline_handler.h"
#include "libcamera/internal/v4l2_request.h"
#include "libcamera/internal/v4l2_subdevice.h"
#include "libcamera/internal/v4l2_videodevice.h"
#include "libcamera/internal/yaml_parser.h"

#include "rkisp1_path.h"
#include "sequence_sync_helper.h"

namespace libcamera {

LOG_DEFINE_CATEGORY(RkISP1)
LOG_DEFINE_CATEGORY(RkISP1Schedule)

class PipelineHandlerRkISP1;
class RkISP1CameraData;



class RkISP1CameraData : public Camera::Private
{
public:
	RkISP1CameraData(PipelineHandler *pipe, RkISP1MainPath *mainPath,
			 RkISP1SelfPath *selfPath)
		: Camera::Private(pipe), frame_(0),
		  mainPath_(mainPath), selfPath_(selfPath)
	{
	}

	PipelineHandlerRkISP1 *pipe();
	const PipelineHandlerRkISP1 *pipe() const;
	int loadIPA(unsigned int hwRevision, uint32_t supportedBlocks);

	Stream mainPathStream_;
	Stream selfPathStream_;
	std::unique_ptr<CameraSensor> sensor_;
	std::unique_ptr<DelayedControls> delayedCtrls_;
	/*
	 * The sensor frame sequence of the last request queued to the pipeline
	 * handler.
	 */
	unsigned int frame_;
	std::vector<IPABuffer> ipaBuffers_;

	RkISP1MainPath *mainPath_;
	RkISP1SelfPath *selfPath_;

	std::unique_ptr<ipa::rkisp1::IPAProxyRkISP1> ipa_;

	ControlInfoMap ipaControls_;

	/*
	 * All entities in the pipeline, from the camera sensor to the RKISP1.
	 */
	MediaPipeline pipe_;

	struct DewarpParms {
		Matrix<double, 3, 3> cm;
		std::vector<double> coeffs;
	};
	std::optional<DewarpParms> dewarpParams_;
	bool canUseDewarper_;
	bool usesDewarper_;

private:
	void paramsComputed(unsigned int frame, unsigned int bytesused);
	void setSensorControls(unsigned int frame,
			       const ControlList &sensorControls);

	void metadataReady(unsigned int frame, const ControlList &metadata);
	int loadTuningFile(const std::string &file);
};

class RkISP1CameraConfiguration : public CameraConfiguration
{
public:
	RkISP1CameraConfiguration(Camera *camera, RkISP1CameraData *data);

	Status validate() override;

	const V4L2SubdeviceFormat &sensorFormat() { return sensorFormat_; }
	const Transform &combinedTransform() { return combinedTransform_; }

private:
	bool fitsAllPaths(const StreamConfiguration &cfg);

	/*
	 * The RkISP1CameraData instance is guaranteed to be valid as long as the
	 * corresponding Camera instance is valid. In order to borrow a
	 * reference to the camera data, store a new reference to the camera.
	 */
	std::shared_ptr<Camera> camera_;
	const RkISP1CameraData *data_;

	V4L2SubdeviceFormat sensorFormat_;
	Transform combinedTransform_;
};

struct SensorFrameInfo {
	Request *request = nullptr;
	FrameBuffer *statsBuffer = nullptr;
	ControlList metadata;
	bool metadataProcessed = false;
};

struct RequestInfo {
	Request *request = nullptr;
	/*
	 * The estimated sensor sequence for this request. Only reliable when
	 * sequenceValid is true
	 */
	size_t sequence = 0;
	bool sequenceValid = false;
};

struct ParamBufferInfo {
	FrameBuffer *buffer = nullptr;
	size_t expectedSequence = 0;
};

struct DewarpBufferInfo {
	FrameBuffer *inputBuffer;
	FrameBuffer *outputBuffer;
};

namespace {

/*
 * This many buffers ensures that the pipeline runs smoothly, without frame
 * drops.
 */
static constexpr unsigned int kRkISP1MinBufferCount = 6;

/*
 * This many internal buffers (params and stats) are needed for smooth operation
 * \todo In high framerate or high cpu load situations it might be necessary to
 * increase this number. \todo: This also relates to max sensor delay and must
 * always be >= maxSensor delay
 */
static constexpr unsigned int kRkISP1InternalBufferCount = 4;

/*
 * This many internal image buffers between ISP and dewarper are needed for
 * smooth operation.
 */
static constexpr unsigned int kRkISP1DewarpImageBufferCount = 4;

/*
 * This flag allows to use dynamic dewarp maps to support pan, zoom, rotate when
 * the kernel driver doesn't support requests. Only needed for legacy customer
 * kernels.
 */
static constexpr bool kAllowDynamicDewarpMapsWithoutRequests = true;

} /* namespace */

class PipelineHandlerRkISP1 : public PipelineHandler
{
public:
	PipelineHandlerRkISP1(CameraManager *manager);

	std::unique_ptr<CameraConfiguration> generateConfiguration(Camera *camera,
								   Span<const StreamRole> roles) override;
	int configure(Camera *camera, CameraConfiguration *config) override;

	int exportFrameBuffers(Camera *camera, Stream *stream,
			       std::vector<std::unique_ptr<FrameBuffer>> *buffers) override;

	int start(Camera *camera, const ControlList *controls) override;
	void stopDevice(Camera *camera) override;

	int queueRequestDevice(Camera *camera, Request *request) override;

	bool match(DeviceEnumerator *enumerator) override;

private:
	static constexpr Size kRkISP1PreviewSize = { 1920, 1080 };

	RkISP1CameraData *cameraData(Camera *camera)
	{
		return static_cast<RkISP1CameraData *>(camera->_d());
	}

	friend RkISP1CameraData;
	friend RkISP1CameraConfiguration;

	int initLinks(Camera *camera, const RkISP1CameraConfiguration &config);
	int createCamera(MediaEntity *sensor);
	void tryCompleteRequests();
	void cancelDewarpRequest(Request *request);
	void imageBufferReady(FrameBuffer *buffer);
	void paramBufferReady(FrameBuffer *buffer);
	void statBufferReady(FrameBuffer *buffer);
	void dewarpRequestReady(V4L2Request *request);
	void dewarpBufferReady(FrameBuffer *buffer);
	void frameStart(uint32_t sequence);

	void queueInternalBuffers();
	void computeParamBuffers(uint32_t maxSequence);

	int allocateBuffers(Camera *camera);
	int freeBuffers(Camera *camera);

	int updateControls(RkISP1CameraData *data);

	std::shared_ptr<MediaDevice> media_;
	std::unique_ptr<V4L2Subdevice> isp_;
	std::unique_ptr<V4L2VideoDevice> param_;
	std::unique_ptr<V4L2VideoDevice> stat_;

	bool hasSelfPath_;
	bool isRaw_;

	RkISP1MainPath mainPath_;
	RkISP1SelfPath selfPath_;

	std::unique_ptr<ConverterDW100> dewarper_;

	/* Internal buffers used when dewarper is being used */
	std::vector<std::unique_ptr<FrameBuffer>> mainPathBuffers_;
	std::queue<FrameBuffer *> availableMainPathBuffers_;

	std::vector<std::unique_ptr<V4L2Request>> dewarpRequests_;
	std::queue<V4L2Request *> availableDewarpRequests_;

	bool running_ = false;

	std::vector<std::unique_ptr<FrameBuffer>> paramBuffers_;
	std::vector<std::unique_ptr<FrameBuffer>> statBuffers_;
	std::queue<FrameBuffer *> availableParamBuffers_;
	std::queue<FrameBuffer *> availableStatBuffers_;

	std::deque<RequestInfo> queuedRequests_;

	std::map<unsigned int, SensorFrameInfo> sensorFrameInfos_;

	std::deque<DewarpBufferInfo> queuedDewarpBuffers_;
	SequenceSyncHelper paramsSyncHelper_;
	SequenceSyncHelper imageSyncHelper_;

	std::queue<ParamBufferInfo> computingParamBuffers_;
	std::queue<ParamBufferInfo> queuedParamBuffers_;


	uint32_t nextParamsSequence_;
	uint32_t nextStatsToProcess_;

	Camera *activeCamera_;
};



PipelineHandlerRkISP1 *RkISP1CameraData::pipe()
{
	return static_cast<PipelineHandlerRkISP1 *>(Camera::Private::pipe());
}

const PipelineHandlerRkISP1 *RkISP1CameraData::pipe() const
{
	return static_cast<const PipelineHandlerRkISP1 *>(Camera::Private::pipe());
}

int RkISP1CameraData::loadIPA(unsigned int hwRevision, uint32_t supportedBlocks)
{
	ipa_ = IPAManager::createIPA<ipa::rkisp1::IPAProxyRkISP1>(pipe(), 1, 1);
	if (!ipa_)
		return -ENOENT;

	ipa_->setSensorControls.connect(this, &RkISP1CameraData::setSensorControls);
	ipa_->paramsComputed.connect(this, &RkISP1CameraData::paramsComputed);
	ipa_->metadataReady.connect(this, &RkISP1CameraData::metadataReady);

	/* The IPA tuning file is made from the sensor name. */
	std::string ipaTuningFile =
		ipa_->configurationFile(sensor_->model() + ".yaml", "uncalibrated.yaml");

	IPACameraSensorInfo sensorInfo{};
	int ret = sensor_->sensorInfo(&sensorInfo);
	if (ret) {
		LOG(RkISP1, Error) << "Camera sensor information not available";
		return ret;
	}

	ret = ipa_->init({ ipaTuningFile, sensor_->model() }, hwRevision,
			 supportedBlocks, sensorInfo, sensor_->controls(),
			 &ipaControls_);
	if (ret < 0) {
		LOG(RkISP1, Error) << "IPA initialization failure";
		return ret;
	}

	ret = loadTuningFile(ipaTuningFile);
	if (ret < 0) {
		LOG(RkISP1, Error) << "Failed to load tuning file";
		return ret;
	}

	return 0;
}

int RkISP1CameraData::loadTuningFile(const std::string &path)
{
	if (!pipe()->dewarper_)
		/* Nothing to do without dewarper */
		return 0;

	LOG(RkISP1, Debug) << "Load tuning file " << path;

	File file(path);
	if (!file.open(File::OpenModeFlag::ReadOnly)) {
		int ret = file.error();
		LOG(RkISP1, Error)
			<< "Failed to open tuning file "
			<< path << ": " << strerror(-ret);
		return ret;
	}

	std::unique_ptr<libcamera::YamlObject> data = YamlParser::parse(file);
	if (!data)
		return -EINVAL;

	if (!data->contains("algorithms")) {
		LOG(RkISP1, Error)
			<< "Tuning file doesn't contain any algorithm";
		return -EINVAL;
	}

	const auto &algos = (*data)["algorithms"].asList();
	for (const auto &algo : algos) {
		const auto &params = algo["Dewarp"];
		if (params) {
			canUseDewarper_ = true;
			DewarpParms dp;
			if (params["cm"]) {
				const auto &cm = params["cm"].get<Matrix<double, 3, 3>>();
				if (!cm) {
					LOG(RkISP1, Error) << "Dewarp parameters are missing 'cm' value";
					return -EINVAL;
				}
				dp.cm = *cm;
			}

			if (params["coefficients"]) {
				const auto &coeffs = params["coefficients"].getList<double>();
				if (!coeffs) {
					LOG(RkISP1, Error) << "Dewarp parameters 'coefficients' value is not a list";
					return -EINVAL;
				}
				dp.coeffs = *coeffs;
			}

			dewarpParams_ = dp;
		}
	}

	return 0;
}

void RkISP1CameraData::paramsComputed(unsigned int frame, unsigned int bytesused)
{
	PipelineHandlerRkISP1 *pipe = RkISP1CameraData::pipe();
	ParamBufferInfo &pInfo = pipe->computingParamBuffers_.front();
	pipe->computingParamBuffers_.pop();

	ASSERT(pInfo.expectedSequence == frame);
	FrameBuffer *buffer = pInfo.buffer;

	LOG(RkISP1Schedule, Debug) << "Queue params for " << frame << " " << buffer;

	buffer->_d()->metadata().planes()[0].bytesused = bytesused;
	int ret = pipe->param_->queueBuffer(buffer);
	if (ret < 0) {
		LOG(RkISP1, Error) << "Failed to queue parameter buffer: "
				   << strerror(-ret);
		pipe->availableParamBuffers_.push(buffer);
		return;
	}

	pipe->queuedParamBuffers_.push({ buffer, frame });
}

void RkISP1CameraData::setSensorControls(unsigned int frame,
					 const ControlList &sensorControls)
{
	/* We know delayed controls is prewarmed for frame 0 */
	if (frame == 0)
		return;

	LOG(RkISP1Schedule, Debug) << "DelayedControls push " << frame;
	delayedCtrls_->push(frame, sensorControls);
}

void RkISP1CameraData::metadataReady(unsigned int frame, const ControlList &metadata)
{
	PipelineHandlerRkISP1 *pipe = RkISP1CameraData::pipe();

	LOG(RkISP1Schedule, Debug) << " metadataReady " << frame;

	auto &info = pipe->sensorFrameInfos_[frame];

	/*
	 * We don't necessarily know the request for that sequence number,
	 * as the dequeue of the image buffer might not have happened yet.
	 * So we check all known requests and store the metadata otherwise.
	 */
	for (auto &reqInfo : pipe->queuedRequests_) {
		if (!reqInfo.sequenceValid) {
			LOG(RkISP1Schedule, Debug)
				<< "Need to store metadata for later " << frame;
			info.metadata = metadata;
			break;
		}

		if (frame > reqInfo.sequence) {
			/*
			 * We will never get stats for that request. Log an
			 * error and return it.
			 */
			LOG(RkISP1, Warning)
				<< "Stats for frame " << reqInfo.sequence
				<< " got lost";
			auto &info2 = pipe->sensorFrameInfos_[reqInfo.sequence];
			info2.metadataProcessed = true;
			ASSERT(info2.statsBuffer == nullptr);
			continue;
		}

		if (frame == reqInfo.sequence) {
			reqInfo.request->metadata().merge(metadata);
			break;
		}

		/* We should never end up here */
		LOG(RkISP1, Error) << "Request for sequence " << frame
				   << " is already handled. Metadata was too late";

		break;
	}

	info.metadataProcessed = true;
	/*
	 * info.statsBuffer can be null, if ipa->processStats() was called
	 * without a buffer to just fill the metadata.
	 */
	if (info.statsBuffer)
		pipe->availableStatBuffers_.push(info.statsBuffer);
	info.statsBuffer = nullptr;

	pipe->tryCompleteRequests();
	pipe->queueInternalBuffers();
}

/* -----------------------------------------------------------------------------
 * Camera Configuration
 */

namespace {

/* Keep in sync with the supported raw formats in rkisp1_path.cpp. */
const std::map<PixelFormat, uint32_t> rawFormats = {
	{ formats::SBGGR8, MEDIA_BUS_FMT_SBGGR8_1X8 },
	{ formats::SGBRG8, MEDIA_BUS_FMT_SGBRG8_1X8 },
	{ formats::SGRBG8, MEDIA_BUS_FMT_SGRBG8_1X8 },
	{ formats::SRGGB8, MEDIA_BUS_FMT_SRGGB8_1X8 },
	{ formats::SBGGR10, MEDIA_BUS_FMT_SBGGR10_1X10 },
	{ formats::SGBRG10, MEDIA_BUS_FMT_SGBRG10_1X10 },
	{ formats::SGRBG10, MEDIA_BUS_FMT_SGRBG10_1X10 },
	{ formats::SRGGB10, MEDIA_BUS_FMT_SRGGB10_1X10 },
	{ formats::SBGGR12, MEDIA_BUS_FMT_SBGGR12_1X12 },
	{ formats::SGBRG12, MEDIA_BUS_FMT_SGBRG12_1X12 },
	{ formats::SGRBG12, MEDIA_BUS_FMT_SGRBG12_1X12 },
	{ formats::SRGGB12, MEDIA_BUS_FMT_SRGGB12_1X12 },
};

} /* namespace */

RkISP1CameraConfiguration::RkISP1CameraConfiguration(Camera *camera,
						     RkISP1CameraData *data)
	: CameraConfiguration()
{
	camera_ = camera->shared_from_this();
	data_ = data;
}

bool RkISP1CameraConfiguration::fitsAllPaths(const StreamConfiguration &cfg)
{
	const CameraSensor *sensor = data_->sensor_.get();
	StreamConfiguration config;

	config = cfg;
	if (data_->mainPath_->validate(sensor, sensorConfig, &config) != Valid)
		return false;

	config = cfg;
	if (data_->selfPath_ &&
	    data_->selfPath_->validate(sensor, sensorConfig, &config) != Valid)
		return false;

	return true;
}

CameraConfiguration::Status RkISP1CameraConfiguration::validate()
{
	const PipelineHandlerRkISP1 *pipe = data_->pipe();
	const CameraSensor *sensor = data_->sensor_.get();
	unsigned int pathCount = data_->selfPath_ ? 2 : 1;
	Status status;

	if (config_.empty())
		return Invalid;

	status = validateColorSpaces(ColorSpaceFlag::StreamsShareColorSpace);

	/*
	 * Make sure that if a sensor configuration has been requested it
	 * is valid.
	 */
	if (sensorConfig) {
		if (!sensorConfig->isValid()) {
			LOG(RkISP1, Error)
				<< "Invalid sensor configuration request";

			return Invalid;
		}

		unsigned int bitDepth = sensorConfig->bitDepth;
		if (bitDepth != 8 && bitDepth != 10 && bitDepth != 12) {
			LOG(RkISP1, Error)
				<< "Invalid sensor configuration bit depth";

			return Invalid;
		}
	}

	/* Cap the number of entries to the available streams. */
	if (config_.size() > pathCount) {
		config_.resize(pathCount);
		status = Adjusted;
	}

	/*
	 * Simultaneous capture of raw and processed streams isn't possible. If
	 * there is any raw stream, cap the number of streams to one.
	 */
	bool isRaw = false;
	if (config_.size() > 1) {
		for (const auto &cfg : config_) {
			if (PixelFormatInfo::info(cfg.pixelFormat).colourEncoding ==
			    PixelFormatInfo::ColourEncodingRAW) {
				config_.resize(1);
				status = Adjusted;
				isRaw = true;
				break;
			}
		}
	}

	/*
	 * If the dewarper supports orientation adjustments, apply that completely
	 * there. Even if the sensor supports flips, it is beneficial to do that
	 * in the dewarper so that lens dewarping happens on the unflipped image
	 */
	bool transposeAfterIsp = false;
	bool useDewarper = false;
	if (data_->canUseDewarper_ && !isRaw) {
		useDewarper = true;
		combinedTransform_ = orientation / data_->sensor_->mountingOrientation();
		if (!!(combinedTransform_ & Transform::Transpose))
			transposeAfterIsp = true;
	} else {
		Orientation requestedOrientation = orientation;
		combinedTransform_ = data_->sensor_->computeTransform(&orientation);
		if (orientation != requestedOrientation)
			status = Adjusted;
	}

	/*
	 * If there are more than one stream in the configuration figure out the
	 * order to evaluate the streams. The first stream has the highest
	 * priority but if both main path and self path can satisfy it evaluate
	 * the second stream first as the first stream is guaranteed to work
	 * with whichever path is not used by the second one.
	 */
	std::vector<unsigned int> order(config_.size());
	std::iota(order.begin(), order.end(), 0);
	if (config_.size() == 2 && fitsAllPaths(config_[0]))
		std::reverse(order.begin(), order.end());

	/*
	 * Validate the configuration against the desired path and, if the
	 * platform supports it, the dewarper. While iterating over the
	 * configurations collect the smallest common sensor format.
	 */
	Size accumulatedSensorSize;
	auto validateConfig = [&](StreamConfiguration &cfg, RkISP1Path *path,
				  Stream *stream, Status expectedStatus) {
		StreamConfiguration tryCfg = cfg;

		/* Need to validate the path before the transpose */
		if (transposeAfterIsp)
			tryCfg.size.transpose();

		Status ret = path->validate(sensor, sensorConfig, &tryCfg);
		if (ret == Invalid)
			return false;

		if (!useDewarper &&
		    (expectedStatus == Valid && ret == Adjusted))
			return false;

		Size sensorSize = tryCfg.size;

		if (useDewarper) {
			/*
			 * The dewarper output is independent of the ISP path.
			 * Reset to the originally requested size.
			 */
			tryCfg.size = cfg.size;
			bool adjusted;

			pipe->dewarper_->validateOutput(&tryCfg, &adjusted,
							Converter::Alignment::Down);
			if (expectedStatus == Valid && adjusted)
				return false;
		}

		if (tryCfg.bufferCount < kRkISP1MinBufferCount) {
			if (expectedStatus == Valid)
				return false;
			tryCfg.bufferCount = kRkISP1MinBufferCount;
		}

		cfg = tryCfg;
		cfg.setStream(stream);

		accumulatedSensorSize = std::max(accumulatedSensorSize, sensorSize);
		return true;
	};

	bool mainPathAvailable = true;
	bool selfPathAvailable = data_->selfPath_;
	RkISP1Path *mainPath = data_->mainPath_;
	RkISP1Path *selfPath = data_->selfPath_;
	Stream *mainPathStream = const_cast<Stream *>(&data_->mainPathStream_);
	Stream *selfPathStream = const_cast<Stream *>(&data_->selfPathStream_);
	for (unsigned int index : order) {
		StreamConfiguration &cfg = config_[index];

		/* Try to match stream without adjusting configuration. */
		if (mainPathAvailable) {
			if (validateConfig(cfg, mainPath, mainPathStream, Valid)) {
				mainPathAvailable = false;
				continue;
			}
		}

		if (selfPathAvailable) {
			if (validateConfig(cfg, selfPath, selfPathStream, Valid)) {
				selfPathAvailable = false;
				continue;
			}
		}

		/* Try to match stream allowing adjusting configuration. */
		if (mainPathAvailable) {
			if (validateConfig(cfg, mainPath, mainPathStream, Adjusted)) {
				mainPathAvailable = false;
				status = Adjusted;
				continue;
			}
		}

		if (selfPathAvailable) {
			if (validateConfig(cfg, selfPath, selfPathStream, Adjusted)) {
				selfPathAvailable = false;
				status = Adjusted;
				continue;
			}
		}

		/* All paths rejected configuration. */
		LOG(RkISP1, Debug) << "Camera configuration not supported "
				   << cfg.toString();
		return Invalid;
	}

	std::vector<unsigned int> mbusCodes;

	if (isRaw) {
		mbusCodes = { rawFormats.at(config_[0].pixelFormat) };
	} else {
		std::transform(rawFormats.begin(), rawFormats.end(),
			       std::back_inserter(mbusCodes),
			       [](const auto &value) { return value.second; });
	}

	sensorFormat_ = sensor->getFormat(mbusCodes, accumulatedSensorSize,
					  mainPath->maxResolution());

	if (sensorFormat_.size.isNull()) {
		/*
		 * \todo When can this happen? Should we return a failure in
		 * this case?
		 */
		sensorFormat_.size = sensor->resolution();
		LOG(RkISP1, Warning)
			<< "Failed to select sensor format. Default to "
			<< sensorFormat_;
	}

	return status;
}

/* -----------------------------------------------------------------------------
 * Pipeline Operations
 */

PipelineHandlerRkISP1::PipelineHandlerRkISP1(CameraManager *manager)
	: PipelineHandler(manager, kRkISP1MinBufferCount), hasSelfPath_(true)
{
}

std::unique_ptr<CameraConfiguration>
PipelineHandlerRkISP1::generateConfiguration(Camera *camera,
					     Span<const StreamRole> roles)
{
	RkISP1CameraData *data = cameraData(camera);

	unsigned int pathCount = data->selfPath_ ? 2 : 1;
	if (roles.size() > pathCount) {
		LOG(RkISP1, Error) << "Too many stream roles requested";
		return nullptr;
	}

	std::unique_ptr<CameraConfiguration> config =
		std::make_unique<RkISP1CameraConfiguration>(camera, data);
	if (roles.empty())
		return config;

	Transform transform = Transform::Identity;
	Size previewSize = kRkISP1PreviewSize;
	bool transposeAfterIsp = false;
	if (data->canUseDewarper_) {
		transform = Orientation::Rotate0 / data->sensor_->mountingOrientation();
		if (!!(transform & Transform::Transpose))
			transposeAfterIsp = true;
	}

	/*
	 * In case of a transpose transform we need to create a path for the
	 * transposed size.
	 */
	if (transposeAfterIsp)
		previewSize.transpose();

	/*
	 * As the ISP can't output different color spaces for the main and self
	 * path, pick a sensible default color space based on the role of the
	 * first stream and use it for all streams.
	 */
	std::optional<ColorSpace> colorSpace;
	bool mainPathAvailable = true;

	for (const StreamRole role : roles) {
		Size size;

		switch (role) {
		case StreamRole::StillCapture:
			/* JPEG encoders typically expect sYCC. */
			if (!colorSpace)
				colorSpace = ColorSpace::Sycc;

			size = data->sensor_->resolution();
			break;

		case StreamRole::Viewfinder:
			/*
			 * sYCC is the YCbCr encoding of sRGB, which is commonly
			 * used by displays.
			 */
			if (!colorSpace)
				colorSpace = ColorSpace::Sycc;

			size = previewSize;
			break;

		case StreamRole::VideoRecording:
			/* Rec. 709 is a good default for HD video recording. */
			if (!colorSpace)
				colorSpace = ColorSpace::Rec709;

			size = previewSize;
			break;

		case StreamRole::Raw:
			if (roles.size() > 1) {
				LOG(RkISP1, Error)
					<< "Can't capture both raw and processed streams";
				return nullptr;
			}

			colorSpace = ColorSpace::Raw;
			size = data->sensor_->resolution();
			break;

		default:
			LOG(RkISP1, Warning)
				<< "Requested stream role not supported: " << role;
			return nullptr;
		}

		/*
		 * Prefer the main path if available, as it supports higher
		 * resolutions.
		 *
		 * \todo Using the main path unconditionally hides support for
		 * RGB (only available on the self path) in the streams formats
		 * exposed to applications. This likely calls for a better API
		 * to expose streams capabilities.
		 */
		RkISP1Path *path;
		if (mainPathAvailable) {
			path = data->mainPath_;
			mainPathAvailable = false;
		} else {
			path = data->selfPath_;
		}

		StreamConfiguration cfg =
			path->generateConfiguration(data->sensor_.get(), size, role);
		if (!cfg.pixelFormat.isValid())
			return nullptr;

		if (transposeAfterIsp && role != StreamRole::Raw)
			cfg.size.transpose();

		cfg.colorSpace = colorSpace;
		cfg.bufferCount = kRkISP1MinBufferCount;
		config->addConfiguration(cfg);
	}

	config->validate();

	return config;
}

int PipelineHandlerRkISP1::configure(Camera *camera, CameraConfiguration *c)
{
	RkISP1CameraConfiguration *config =
		static_cast<RkISP1CameraConfiguration *>(c);
	RkISP1CameraData *data = cameraData(camera);
	CameraSensor *sensor = data->sensor_.get();
	int ret;

	ret = initLinks(camera, *config);
	if (ret)
		return ret;

	const PixelFormat &streamFormat = config->at(0).pixelFormat;
	const PixelFormatInfo &info = PixelFormatInfo::info(streamFormat);
	isRaw_ = info.colourEncoding == PixelFormatInfo::ColourEncodingRAW;
	data->usesDewarper_ = data->canUseDewarper_ && !isRaw_;

	Transform transform = config->combinedTransform();
	bool transposeAfterIsp = false;
	if (data->usesDewarper_) {
		if (!!(transform & Transform::Transpose))
			transposeAfterIsp = true;
		transform = Transform::Identity;
	}

	/*
	 * Configure the format on the sensor output and propagate it through
	 * the pipeline.
	 */
	V4L2SubdeviceFormat format = config->sensorFormat();
	LOG(RkISP1, Debug) << "Configuring sensor with " << format;

	if (config->sensorConfig)
		ret = sensor->applyConfiguration(*config->sensorConfig,
						 transform,
						 &format);
	else
		ret = sensor->setFormat(&format, transform);

	if (ret < 0)
		return ret;

	LOG(RkISP1, Debug) << "Sensor configured with " << format;

	/* Propagate format through the internal media pipeline up to the ISP */
	ret = data->pipe_.configure(sensor, &format);
	if (ret < 0)
		return ret;

	LOG(RkISP1, Debug) << "Configuring ISP with : " << format;
	ret = isp_->setFormat(0, &format);
	if (ret < 0)
		return ret;

	Rectangle inputCrop(0, 0, format.size);
	ret = isp_->setSelection(0, V4L2_SEL_TGT_CROP, &inputCrop);
	if (ret < 0)
		return ret;

	LOG(RkISP1, Debug)
		<< "ISP input pad configured with " << format
		<< " crop " << inputCrop;

	Rectangle outputCrop = inputCrop;

	/* YUYV8_2X8 is required on the ISP source path pad for YUV output. */
	if (!isRaw_)
		format.code = MEDIA_BUS_FMT_YUYV8_2X8;

	/*
	 * On devices without DUAL_CROP (like the imx8mp) cropping needs to be
	 * done on the ISP/IS output.
	 *
	 * If the dewarper is used, the cropping shall be done by the dewarper.
	 */
	if (media_->hwRevision() == RKISP1_V_IMX8MP) {
		/* imx8mp has only a single path. */
		const auto &cfg = config->at(0);
		/*
		 * If the dewarper is used, all cropping including aspect ratio
		 * preservation shall be done there. To ensure that the output
		 * format provided by the ISP is supported by the dewarper, a
		 * minimal crop still needs to be applied on the ISP output.
		 *
		 * \todo It might be possible to allocate bigger buffers
		 * (aligned to 8 pixels) with a stride matching format.size for
		 * the ISP. The not-filled border could later be ignored by the
		 * dewarper. This way we could skip the minimal crop here and
		 * the MaximumScalerCrop would always match the isp output.
		 */
		Size ispCrop;
		if (data->usesDewarper_)
			ispCrop = dewarper_->adjustInputSize(cfg.pixelFormat,
							     format.size);
		else
			ispCrop = format.size.boundedToAspectRatio(cfg.size)
					  .alignedUpTo(2, 2);

		outputCrop = ispCrop.centeredTo(Rectangle(format.size).center());
		format.size = ispCrop;
	}

	LOG(RkISP1, Debug)
		<< "Configuring ISP output pad with " << format
		<< " crop " << outputCrop;

	ret = isp_->setSelection(2, V4L2_SEL_TGT_CROP, &outputCrop);
	if (ret < 0)
		return ret;

	format.colorSpace = config->at(0).colorSpace;
	ret = isp_->setFormat(2, &format);
	if (ret < 0)
		return ret;

	LOG(RkISP1, Debug)
		<< "ISP output pad configured with " << format
		<< " crop " << outputCrop;

	IPACameraSensorInfo sensorInfo;
	ret = data->sensor_->sensorInfo(&sensorInfo);
	if (ret)
		return ret;

	std::map<unsigned int, IPAStream> streamConfig;
	std::vector<std::reference_wrapper<StreamConfiguration>> outputCfgs;

	for (const StreamConfiguration &cfg : *config) {
		if (cfg.stream() == &data->mainPathStream_) {
			/*
			 * To allow for digital zoom, scaling down should happen
			 * in the dewarper, instead of the resizer. Configure
			 * the isp output to the same size as the sensor output.
			 */
			StreamConfiguration ispCfg = cfg;
			if (data->usesDewarper_) {
				outputCfgs.push_back(const_cast<StreamConfiguration &>(cfg));

				ispCfg.size = format.size;
				ispCfg.stride =
					PixelFormatInfo::info(ispCfg.pixelFormat)
						.stride(ispCfg.size.width, 0);

				ret = dewarper_->configure(ispCfg, outputCfgs);
				if (ret)
					return ret;

				/*
				 * Apply the actual sensor crop, for proper
				 * dewarp map calculation
				 */
				Rectangle sensorCrop = outputCrop.transformedBetween(
					inputCrop, sensorInfo.analogCrop);
				auto &vertexMap = dewarper_->vertexMap(cfg.stream());
				vertexMap.setSensorCrop(sensorCrop);
				vertexMap.setTransform(config->combinedTransform());
				if (data->dewarpParams_) {
					vertexMap.setDewarpParams(data->dewarpParams_->cm,
								  data->dewarpParams_->coeffs);
				}
				data->properties_.set(properties::ScalerCropMaximum, sensorCrop);

				/*
				 * Apply a default sensor crop that keeps the
				 * aspect ratio.
				 */
				Size size = cfg.size;
				if (transposeAfterIsp)
					size.transpose();
				size = sensorCrop.size().boundedToAspectRatio(size);
				vertexMap.setScalerCrop(
					size.centeredTo(sensorCrop.center()));
			}

			ret = mainPath_.configure(ispCfg, format);
			streamConfig[0] = IPAStream(cfg.pixelFormat,
						    cfg.size);
		} else if (hasSelfPath_) {
			ret = selfPath_.configure(cfg, format);
			streamConfig[1] = IPAStream(cfg.pixelFormat,
						    cfg.size);
		} else {
			return -ENODEV;
		}

		if (ret)
			return ret;
	}

	V4L2DeviceFormat paramFormat;
	paramFormat.fourcc = V4L2PixelFormat(V4L2_META_FMT_RK_ISP1_EXT_PARAMS);
	ret = param_->setFormat(&paramFormat);
	if (ret)
		return ret;

	V4L2DeviceFormat statFormat;
	statFormat.fourcc = V4L2PixelFormat(V4L2_META_FMT_RK_ISP1_STAT_3A);
	ret = stat_->setFormat(&statFormat);
	if (ret)
		return ret;

	/* Inform IPA of stream configuration and sensor controls. */
	ipa::rkisp1::IPAConfigInfo ipaConfig{ sensorInfo,
					      data->sensor_->controls(),
					      paramFormat.fourcc };

	ret = data->ipa_->configure(ipaConfig, streamConfig, &data->ipaControls_);
	if (ret) {
		LOG(RkISP1, Error) << "failed configuring IPA (" << ret << ")";
		return ret;
	}

	return updateControls(data);
}

int PipelineHandlerRkISP1::exportFrameBuffers([[maybe_unused]] Camera *camera, Stream *stream,
					      std::vector<std::unique_ptr<FrameBuffer>> *buffers)
{
	RkISP1CameraData *data = cameraData(camera);
	unsigned int count = stream->configuration().bufferCount;

	if (stream == &data->mainPathStream_) {
		/*
		 * Currently, i.MX8MP is the only platform with DW100 dewarper.
		 * It has mainpath and no self path. Hence, export buffers from
		 * dewarper just for the main path stream, for now.
		 */
		if (data->usesDewarper_)
			return dewarper_->exportBuffers(&data->mainPathStream_, count, buffers);
		else
			return mainPath_.exportBuffers(count, buffers);
	} else if (hasSelfPath_ && stream == &data->selfPathStream_) {
		return selfPath_.exportBuffers(count, buffers);
	}

	return -EINVAL;
}

int PipelineHandlerRkISP1::allocateBuffers(Camera *camera)
{
	RkISP1CameraData *data = cameraData(camera);
	unsigned int ipaBufferId = 1;
	int ret;

	auto errorCleanup = utils::scope_exit{ [&]() {
		paramBuffers_.clear();
		statBuffers_.clear();
		mainPathBuffers_.clear();
	} };

	if (!isRaw_) {
		ret = param_->allocateBuffers(kRkISP1InternalBufferCount, &paramBuffers_);
		if (ret < 0)
			return ret;

		ret = stat_->allocateBuffers(kRkISP1InternalBufferCount, &statBuffers_);
		if (ret < 0)
			return ret;
	}

	/* If the dewarper is being used, allocate internal buffers for ISP. */
	if (data->usesDewarper_) {
		ret = mainPath_.exportBuffers(kRkISP1DewarpImageBufferCount,
					      &mainPathBuffers_);
		if (ret < 0)
			return ret;

		for (std::unique_ptr<FrameBuffer> &buffer : mainPathBuffers_)
			availableMainPathBuffers_.push(buffer.get());

		if (dewarper_->supportsRequests()) {
			ret = dewarper_->allocateRequests(kRkISP1DewarpImageBufferCount + 1,
							  &dewarpRequests_);
			if (ret < 0)
				LOG(RkISP1, Error) << "Failed to allocate requests.";
		}

		for (std::unique_ptr<V4L2Request> &request : dewarpRequests_) {
			request->requestDone.connect(this, &PipelineHandlerRkISP1::dewarpRequestReady);
			availableDewarpRequests_.push(request.get());
		}
	}

	auto pushBuffers = [&](const std::vector<std::unique_ptr<FrameBuffer>> &buffers,
			       std::queue<FrameBuffer *> &queue) {
		for (const std::unique_ptr<FrameBuffer> &buffer : buffers) {
			Span<const FrameBuffer::Plane> planes = buffer->planes();

			buffer->setCookie(ipaBufferId++);
			data->ipaBuffers_.emplace_back(buffer->cookie(),
						       std::vector<FrameBuffer::Plane>{ planes.begin(),
											planes.end() });
			queue.push(buffer.get());
		}
	};

	pushBuffers(paramBuffers_, availableParamBuffers_);
	pushBuffers(statBuffers_, availableStatBuffers_);

	data->ipa_->mapBuffers(data->ipaBuffers_);

	errorCleanup.release();
	return 0;
}

int PipelineHandlerRkISP1::freeBuffers(Camera *camera)
{
	RkISP1CameraData *data = cameraData(camera);

	while (!availableStatBuffers_.empty())
		availableStatBuffers_.pop();

	while (!availableParamBuffers_.empty())
		availableParamBuffers_.pop();

	while (!availableMainPathBuffers_.empty())
		availableMainPathBuffers_.pop();

	paramBuffers_.clear();
	statBuffers_.clear();
	mainPathBuffers_.clear();

	while (!availableDewarpRequests_.empty())
		availableDewarpRequests_.pop();

	dewarpRequests_.clear();

	std::vector<unsigned int> ids;
	for (IPABuffer &ipabuf : data->ipaBuffers_)
		ids.push_back(ipabuf.id);

	data->ipa_->unmapBuffers(ids);
	data->ipaBuffers_.clear();

	if (param_->releaseBuffers())
		LOG(RkISP1, Error) << "Failed to release parameters buffers";

	if (stat_->releaseBuffers())
		LOG(RkISP1, Error) << "Failed to release stat buffers";

	return 0;
}

int PipelineHandlerRkISP1::start(Camera *camera, [[maybe_unused]] const ControlList *controls)
{
	RkISP1CameraData *data = cameraData(camera);
	utils::ScopeExitActions actions;
	int ret;

	/* Allocate buffers for internal pipeline usage. */
	ret = allocateBuffers(camera);
	if (ret)
		return ret;
	actions += [&]() { freeBuffers(camera); };

	ControlList ctrls;
	if (!!controls)
		ctrls = *controls;

	ipa::rkisp1::StartResult res;
	data->ipa_->start(ctrls, &res);
	if (res.code) {
		LOG(RkISP1, Error)
			<< "Failed to start IPA " << camera->id();
		return ret;
	}
	actions += [&]() { data->ipa_->stop(); };
	data->sensor_->setControls(&res.controls);
	data->delayedCtrls_->reset();

	data->frame_ = 0;
	nextParamsSequence_ = 0;
	nextStatsToProcess_ = 0;
	paramsSyncHelper_.reset();
	imageSyncHelper_.reset();

	if (!isRaw_) {
		ret = param_->streamOn();
		if (ret) {
			LOG(RkISP1, Error)
				<< "Failed to start parameters " << camera->id();
			return ret;
		}
		actions += [&]() { param_->streamOff(); };

		ret = stat_->streamOn();
		if (ret) {
			LOG(RkISP1, Error)
				<< "Failed to start statistics " << camera->id();
			return ret;
		}
		actions += [&]() { stat_->streamOff(); };

		if (data->usesDewarper_) {
			/*
			 * Apply the vertex map before start to partially
			 * support ScalerCrop on kernels with a dw100 driver
			 * that has no dynamic vertex map/requests support.
			 */
			if (controls && controls->contains(controls::ScalerCrop.id())) {
				const auto &crop = controls->get(controls::ScalerCrop);
				auto &vertexMap = dewarper_->vertexMap(&data->mainPathStream_);
				vertexMap.setScalerCrop(*crop);
			}
			dewarper_->applyVertexMap(&data->mainPathStream_);

			ret = dewarper_->start();
			if (ret) {
				LOG(RkISP1, Error) << "Failed to start dewarper";
				return ret;
			}
			actions += [&]() { dewarper_->stop(); };
		}
	}

	if (data->mainPath_->isEnabled()) {
		ret = mainPath_.start(data->mainPathStream_.configuration().bufferCount);
		if (ret)
			return ret;
		actions += [&]() { mainPath_.stop(); };
	}

	if (hasSelfPath_ && data->selfPath_->isEnabled()) {
		ret = selfPath_.start(data->selfPathStream_.configuration().bufferCount);
		if (ret)
			return ret;
	}

	isp_->setFrameStartEnabled(true);

	activeCamera_ = camera;
	running_ = true;

	queueInternalBuffers();

	actions.release();
	return 0;
}

void PipelineHandlerRkISP1::stopDevice(Camera *camera)
{
	RkISP1CameraData *data = cameraData(camera);
	int ret;
	running_ = false;

	LOG(RkISP1Schedule, Debug) << "Stop device";

	isp_->setFrameStartEnabled(false);

	data->ipa_->stop();

	if (hasSelfPath_)
		selfPath_.stop();
	mainPath_.stop();

	if (!isRaw_) {
		ret = stat_->streamOff();
		if (ret)
			LOG(RkISP1, Warning)
				<< "Failed to stop statistics for " << camera->id();

		ret = param_->streamOff();
		if (ret)
			LOG(RkISP1, Warning)
				<< "Failed to stop parameters for " << camera->id();

		/*
		 * The param buffers are not returned in order, so the queue
		 * becomes useless.
		 */
		queuedParamBuffers_ = {};

		if (data->usesDewarper_)
			dewarper_->stop();
	}

	tryCompleteRequests();

	/* There can still be requests that are either waiting for metadata
	   or that contain buffers which were not yet queued at all. */
	while (!queuedRequests_.empty()) {
		RequestInfo &reqInfo = queuedRequests_.front();
		cancelRequest(reqInfo.request);
		queuedRequests_.pop_front();
	}
	sensorFrameInfos_.clear();

	ASSERT(queuedDewarpBuffers_.empty());
	ASSERT(queuedParamBuffers_.empty());
	ASSERT(computingParamBuffers_.empty());

	freeBuffers(camera);

	activeCamera_ = nullptr;
}

void PipelineHandlerRkISP1::queueInternalBuffers()
{
	if (!running_)
		return;

	RkISP1CameraData *data = cameraData(activeCamera_);

	while (!availableStatBuffers_.empty()) {
		FrameBuffer *buf = availableStatBuffers_.front();
		availableStatBuffers_.pop();
		data->pipe()->stat_->queueBuffer(buf);
	}

	/*
	 * In case of the dewarper, there is a seperate buffer loop for the main
	 * path
	 */
	while (!availableMainPathBuffers_.empty()) {
		FrameBuffer *buf = availableMainPathBuffers_.front();
		availableMainPathBuffers_.pop();

		LOG(RkISP1Schedule, Debug) << "Queue mainPath " << buf;
		data->mainPath_->queueBuffer(buf);
	}
}

void PipelineHandlerRkISP1::computeParamBuffers(uint32_t maxSequence)
{
	RkISP1CameraData *data = cameraData(activeCamera_);
	if (isRaw_) {
		/*
		 * Call computeParams with an empty param buffer to trigger
		 * the setSensorControls signal.
		 */
		data->ipa_->computeParams(maxSequence, 0);
		return;
	}

	while (nextParamsSequence_ <= maxSequence) {
		if (availableParamBuffers_.empty()) {
			LOG(RkISP1Schedule, Warning)
				<< "Ran out of parameter buffers";
			return;
		}

		int correction = paramsSyncHelper_.correction();
		if (correction != 0)
			LOG(RkISP1Schedule, Warning)
				<< "Correcting params sequence "
				<< correction;

		uint32_t paramsSequence;
		if (correction >= 0) {
			nextParamsSequence_ += correction;
			paramsSyncHelper_.pushCorrection(correction);
			paramsSequence = nextParamsSequence_++;
		} else {
			/*
			 * Inject the same sequence multiple times, to correct
			 * for the offset.
			 */
			paramsSyncHelper_.pushCorrection(-1);
			paramsSequence = nextParamsSequence_;
		}

		FrameBuffer *buf = availableParamBuffers_.front();
		availableParamBuffers_.pop();
		computingParamBuffers_.push({ buf, paramsSequence });
		LOG(RkISP1Schedule, Debug) << "Request params for " << paramsSequence;
		data->ipa_->computeParams(paramsSequence, buf->cookie());
	}
}

int PipelineHandlerRkISP1::queueRequestDevice(Camera *camera, Request *request)
{
	RkISP1CameraData *data = cameraData(camera);

	RequestInfo info;
	info.request = request;

	int correction = imageSyncHelper_.correction();
	if (correction != 0)
		LOG(RkISP1Schedule, Debug)
			<< "Correcting image sequence "
			<< data->frame_ << " to " << data->frame_ + correction;
	data->frame_ += correction;
	imageSyncHelper_.pushCorrection(correction);
	info.sequence = data->frame_;
	data->frame_++;

	LOG(RkISP1Schedule, Debug) << "Queue request. Request sequence: "
				   << request->sequence()
				   << " estimated sensor frame sequence: " << info.sequence
				   << " queue size: " << (queuedRequests_.size() + 1);

	data->ipa_->queueRequest(info.sequence, request->controls());

	/*
	 * When the dewarper is used, the request buffers will be queued in
	 * imageBufferReady()
	 */
	if (!data->usesDewarper_) {
		FrameBuffer *mainPathBuffer = request->findBuffer(&data->mainPathStream_);
		FrameBuffer *selfPathBuffer = request->findBuffer(&data->selfPathStream_);
		if (mainPathBuffer)
			data->mainPath_->queueBuffer(mainPathBuffer);

		if (data->selfPath_ && selfPathBuffer)
			data->selfPath_->queueBuffer(selfPathBuffer);
	}

	queuedRequests_.push_back(info);

	/* Kickstart computation of parameters. */
	if (info.sequence < kRkISP1InternalBufferCount)
		computeParamBuffers(info.sequence);

	return 0;
}

/* -----------------------------------------------------------------------------
 * Match and Setup
 */

int PipelineHandlerRkISP1::initLinks(Camera *camera,
				     const RkISP1CameraConfiguration &config)
{
	RkISP1CameraData *data = cameraData(camera);
	int ret;

	ret = media_->disableLinks();
	if (ret < 0)
		return ret;

	/*
	 * Configure the sensor links: enable the links corresponding to this
	 * pipeline all the way up to the ISP, through any connected CSI receiver.
	 */
	ret = data->pipe_.initLinks();
	if (ret) {
		LOG(RkISP1, Error) << "Failed to set up pipe links";
		return ret;
	}

	/* Configure the paths after the ISP */
	for (const StreamConfiguration &cfg : config) {
		if (cfg.stream() == &data->mainPathStream_)
			ret = data->mainPath_->setEnabled(true);
		else if (hasSelfPath_ && cfg.stream() == &data->selfPathStream_)
			ret = data->selfPath_->setEnabled(true);
		else
			return -EINVAL;

		if (ret < 0)
			return ret;
	}

	return 0;
}

/**
 * \brief Update the camera controls
 * \param[in] data The camera data
 *
 * Compute the camera controls by calculating controls which the pipeline
 * is reponsible for and merge them with the controls computed by the IPA.
 *
 * This function needs data->ipaControls_ to be refreshed when a new
 * configuration is applied to the camera by the IPA configure() function.
 *
 * Always call this function after IPA configure() to make sure to have a
 * properly refreshed IPA controls list.
 *
 * \return 0 on success or a negative error code otherwise
 */
int PipelineHandlerRkISP1::updateControls(RkISP1CameraData *data)
{
	ControlInfoMap::Map controls;

	if (data->usesDewarper_) {
		std::pair<Rectangle, Rectangle> cropLimits;
		if (dewarper_->isConfigured(&data->mainPathStream_)) {
			auto &vertexMap = dewarper_->vertexMap(&data->mainPathStream_);
			vertexMap.applyLimits();
			cropLimits = vertexMap.scalerCropBounds();
			controls[&controls::ScalerCrop] = ControlInfo(cropLimits.first,
								      cropLimits.second,
								      vertexMap.effectiveScalerCrop());
		} else {
			/* This happens before configure() has run. Maybe we need a better solution.*/
			/*
			* ScalerCrop is specified to be in Sensor coordinates.
			* So we need to transform the limits to sensor coordinates.
			* We can safely assume that the maximum crop limit contains the
			* full fov of the dewarper.
			*/
			cropLimits = dewarper_->inputCropBounds();
			Rectangle maxCrop = Rectangle(data->sensor_->resolution());
			Rectangle min = cropLimits.first.transformedBetween(cropLimits.second,
									    maxCrop);

			controls[&controls::ScalerCrop] = ControlInfo(min,
								      maxCrop,
								      maxCrop);
		}

		if (dewarper_->supportsRequests() || kAllowDynamicDewarpMapsWithoutRequests) {
			controls[&controls::draft::Dw100Scale] = ControlInfo(0.2f, 8.0f, 1.0f);
			controls[&controls::draft::Dw100Rotation] = ControlInfo(-180.0f, 180.0f, 0.0f);
			controls[&controls::draft::Dw100Offset] = ControlInfo(Point(-10000, -10000), Point(10000, 10000), Point(0, 0));
			controls[&controls::draft::Dw100ScaleMode] = ControlInfo(controls::draft::Dw100ScaleModeValues, controls::draft::Fill);

			if (data->dewarpParams_.has_value())
				controls[&controls::LensDewarpEnable] = ControlInfo(false, true, true);
		} else {
			LOG(RkISP1, Warning)
				<< "dw100 kernel driver has no requests support."
				   " No dynamic configuration possible.";
		}
	}

	/* Add the IPA registered controls to list of camera controls. */
	for (const auto &ipaControl : data->ipaControls_)
		controls[ipaControl.first] = ipaControl.second;

	data->controlInfo_ = ControlInfoMap(std::move(controls),
					    controls::controls);

	return 0;
}

/*
 * By default we assume all the blocks that were included in the first
 * extensible parameters series are available. That is the lower 20bits.
 */
const uint32_t kDefaultExtParamsBlocks = 0xfffff;

int PipelineHandlerRkISP1::createCamera(MediaEntity *sensor)
{
	utils::ScopeExitActions actions;
	int ret;

	std::unique_ptr<RkISP1CameraData> data =
		std::make_unique<RkISP1CameraData>(this, &mainPath_,
						   hasSelfPath_ ? &selfPath_ : nullptr);

	/* Identify the pipeline path between the sensor and the rkisp1_isp */
	ret = data->pipe_.init(sensor, "rkisp1_isp");
	if (ret) {
		LOG(RkISP1, Error) << "Failed to identify path from sensor to sink";
		return ret;
	}

	data->sensor_ = CameraSensorFactoryBase::create(sensor);
	if (!data->sensor_)
		return -ENODEV;

	/* Initialize the camera properties. */
	data->properties_ = data->sensor_->properties();

	const CameraSensorProperties::SensorDelays &delays = data->sensor_->sensorDelays();
	std::unordered_map<uint32_t, DelayedControls::ControlParams> params = {
		{ V4L2_CID_ANALOGUE_GAIN, { delays.gainDelay, false } },
		{ V4L2_CID_EXPOSURE, { delays.exposureDelay, false } },
		{ V4L2_CID_VBLANK, { delays.vblankDelay, false } },
	};

	data->delayedCtrls_ =
		std::make_unique<DelayedControls>(data->sensor_->device(),
						  params);
	isp_->frameStart.connect(this,
				 &PipelineHandlerRkISP1::frameStart);

	actions += [&]() { isp_->frameStart.disconnect(this); };

	uint32_t supportedBlocks = kDefaultExtParamsBlocks;

	auto &controls = param_->controls();
	if (controls.find(RKISP1_CID_SUPPORTED_PARAMS_BLOCKS) != controls.end()) {
		auto list = param_->getControls({ { RKISP1_CID_SUPPORTED_PARAMS_BLOCKS } });
		if (!list.empty())
			supportedBlocks = static_cast<uint32_t>(
				list.get(RKISP1_CID_SUPPORTED_PARAMS_BLOCKS)
					.get<int32_t>());
	} else {
		LOG(RkISP1, Error)
			<< "Failed to query supported params blocks. Falling back to defaults.";
	}

	ret = data->loadIPA(media_->hwRevision(), supportedBlocks);
	if (ret)
		return ret;

	updateControls(data.get());

	std::set<Stream *> streams{
		&data->mainPathStream_,
		&data->selfPathStream_,
	};
	const std::string &id = data->sensor_->id();
	std::shared_ptr<Camera> camera =
		Camera::create(std::move(data), id, streams);

	registerCamera(std::move(camera));

	actions.release();

	return 0;
}

void PipelineHandlerRkISP1::frameStart(uint32_t sequence)
{
	if (!activeCamera_)
		return;

	RkISP1CameraData *data = cameraData(activeCamera_);
	LOG(RkISP1Schedule, Debug) << "frameStart " << sequence;
	uint32_t sequenceToApply = sequence + data->delayedCtrls_->maxDelay();
	data->delayedCtrls_->applyControls(sequenceToApply);

	computeParamBuffers(sequenceToApply + 1);
}

bool PipelineHandlerRkISP1::match(DeviceEnumerator *enumerator)
{
	DeviceMatch dm("rkisp1");
	dm.add("rkisp1_isp");
	dm.add("rkisp1_resizer_mainpath");
	dm.add("rkisp1_mainpath");
	dm.add("rkisp1_stats");
	dm.add("rkisp1_params");

	media_ = acquireMediaDevice(enumerator, dm);
	if (!media_)
		return false;

	if (!media_->hwRevision()) {
		LOG(RkISP1, Error)
			<< "The rkisp1 driver is too old, v5.11 or newer is required";
		return false;
	}

	hasSelfPath_ = !!media_->getEntityByName("rkisp1_selfpath");

	/* Create the V4L2 subdevices we will need. */
	isp_ = V4L2Subdevice::fromEntityName(media_, "rkisp1_isp");
	if (isp_->open() < 0)
		return false;

	/* Locate and open the stats and params video nodes. */
	stat_ = V4L2VideoDevice::fromEntityName(media_, "rkisp1_stats");
	if (stat_->open() < 0)
		return false;

	param_ = V4L2VideoDevice::fromEntityName(media_, "rkisp1_params");
	if (param_->open() < 0)
		return false;

	/* Locate and open the ISP main and self paths. */
	if (!mainPath_.init(media_))
		return false;

	if (hasSelfPath_ && !selfPath_.init(media_))
		return false;

	mainPath_.bufferReady().connect(this, &PipelineHandlerRkISP1::imageBufferReady);
	if (hasSelfPath_)
		selfPath_.bufferReady().connect(this, &PipelineHandlerRkISP1::imageBufferReady);
	stat_->bufferReady.connect(this, &PipelineHandlerRkISP1::statBufferReady);
	param_->bufferReady.connect(this, &PipelineHandlerRkISP1::paramBufferReady);

	/* If dewarper is present, create its instance. */
	DeviceMatch dwp("dw100");
	dwp.add("dw100-source");
	dwp.add("dw100-sink");

	std::shared_ptr<MediaDevice> dwpMediaDevice = enumerator->search(dwp);
	if (dwpMediaDevice) {
		dewarper_ = std::make_unique<ConverterDW100>(dwpMediaDevice);
		if (dewarper_->isValid()) {
			dewarper_->outputBufferReady.connect(
				this, &PipelineHandlerRkISP1::dewarpBufferReady);

			LOG(RkISP1, Info)
				<< "Found DW100 dewarper " << dewarper_->deviceNode();
		} else {
			LOG(RkISP1, Warning)
				<< "Found DW100 dewarper " << dewarper_->deviceNode()
				<< " but invalid";

			dewarper_.reset();
		}
	}

	/*
	 * Enumerate all sensors connected to the ISP and create one
	 * camera instance for each of them.
	 */
	bool registered = false;

	for (MediaEntity *entity : media_->locateEntities(MEDIA_ENT_F_CAM_SENSOR)) {
		LOG(RkISP1, Debug) << "Identified " << entity->name();
		if (!createCamera(entity))
			registered = true;
	}

	return registered;
}

/* -----------------------------------------------------------------------------
 * Buffer Handling
 */

void PipelineHandlerRkISP1::tryCompleteRequests()
{
	std::optional<size_t> lastDeletedSequence;

	/* Complete finished requests */
	while (!queuedRequests_.empty()) {
		RequestInfo info = queuedRequests_.front();

		if (info.request->hasPendingBuffers())
			break;

		if (!info.sequenceValid)
			break;

		if (!sensorFrameInfos_[info.sequence].metadataProcessed)
			break;

		queuedRequests_.pop_front();

		LOG(RkISP1Schedule, Debug) << "Complete request " << info.sequence;
		completeRequest(info.request);

		sensorFrameInfos_[info.sequence].request = nullptr;
		lastDeletedSequence = info.sequence;
	}

	if (!lastDeletedSequence.has_value())
		return;

	/* Drop all outdated sensor frame infos. */
	while (!sensorFrameInfos_.empty()) {
		auto iter = sensorFrameInfos_.begin();
		if (iter->first > lastDeletedSequence.value())
			break;

		ASSERT(iter->second.request == nullptr);
		ASSERT(iter->second.statsBuffer == nullptr);

		sensorFrameInfos_.erase(iter);
	}
}

void PipelineHandlerRkISP1::cancelDewarpRequest(Request *request)
{
	RkISP1CameraData *data = cameraData(activeCamera_);
	/*
	 * i.MX8MP is the only known platform with dewarper. It has
	 * no self path. Hence, only main path buffer completion is
	 * required.
	 *
	 * Also, we cannot completeBuffer(request, buffer) as buffer
	 * here, is an internal buffer (between ISP and dewarper) and
	 * is not associated to the any specific request. The request
	 * buffer associated with main path stream is the one that
	 * is required to be completed (not the internal buffer).
	 */
	for (auto [stream, buffer] : request->buffers()) {
		if (stream == &data->mainPathStream_) {
			buffer->_d()->cancel();
			completeBuffer(request, buffer);
		}
	}

	tryCompleteRequests();
}

void PipelineHandlerRkISP1::imageBufferReady(FrameBuffer *buffer)
{
	ASSERT(activeCamera_);
	RkISP1CameraData *data = cameraData(activeCamera_);
	const FrameMetadata &metadata = buffer->metadata();
	RequestInfo *reqInfo = nullptr;

	/*
	 * When the dewarper is used, the buffer is not yet tied to a request,
	 * so find the first request without a valid sequence. Otherwise find
	 * the request for that buffer. This is not necessarily the same,
	 * because after streamoff the buffers are returned in arbitrary order.
	 */
	for (auto &info : queuedRequests_) {
		if (data->usesDewarper_) {
			if (!info.sequenceValid) {
				reqInfo = &info;
				break;
			}
		} else {
			if (info.request == buffer->request()) {
				reqInfo = &info;
			}
		}
	}

	if (!reqInfo) {
		if (data->usesDewarper_) {
			LOG(RkISP1Schedule, Info)
				<< "Image buffer ready, but no corresponding request";
			availableMainPathBuffers_.push(buffer);
			return;
		}

		LOG(RkISP1Schedule, Fatal)
			<< "Image buffer ready, but no corresponding request";
	}

	Request *request = reqInfo->request;

	LOG(RkISP1Schedule, Debug) << "Image buffer ready: " << buffer
				   << " Expected sequence: " << reqInfo->sequence
				   << " got: " << metadata.sequence;

	uint32_t sequence = metadata.sequence;

	/*
	 * If the frame was cancelled, the metadata sequnce is usually wrong and
	 * we assume that our guess was right.
	 */
	if (metadata.status == FrameMetadata::FrameCancelled)
		sequence = reqInfo->sequence;

	/* We now know the buffer sequence that belongs to this request */
	int droppedFrames = imageSyncHelper_.gotFrame(reqInfo->sequence, sequence);
	if (droppedFrames != 0)
		LOG(RkISP1Schedule, Warning)
			<< "Frame " << reqInfo->sequence << ": Dropped "
			<< droppedFrames << " frames";

	reqInfo->sequence = sequence;
	reqInfo->sequenceValid = true;

	if (sensorFrameInfos_[sequence].metadataProcessed) {
		LOG(RkISP1Schedule, Debug)
			<< "Apply stored metadata " << reqInfo->sequence;
		request->metadata().merge(sensorFrameInfos_[sequence].metadata);
	}

	if (metadata.status != FrameMetadata::FrameCancelled) {
		/*
		 * Record the sensor's timestamp in the request metadata.
		 *
		 * \todo The sensor timestamp should be better estimated by
		 * connecting to the V4L2Device::frameStart signal.
		 */
		request->metadata().set(controls::SensorTimestamp,
					metadata.timestamp);

		/* In raw mode call processStats() to fill the metadata */
		if (isRaw_) {
			const ControlList &ctrls =
				data->delayedCtrls_->get(metadata.sequence);
			data->ipa_->processStats(reqInfo->sequence, 0, ctrls);
		}
	} else {
		/* No need to block waiting for metedata on that frame. */
		sensorFrameInfos_[sequence].metadataProcessed = true;
	}

	if (!data->usesDewarper_) {
		completeBuffer(reqInfo->request, buffer);
		tryCompleteRequests();

		return;
	}

	/* Do not queue cancelled frames to the dewarper. */
	if (metadata.status == FrameMetadata::FrameCancelled) {
		cancelDewarpRequest(reqInfo->request);
		return;
	}

	V4L2Request *dewarpRequest = nullptr;
	if (!dewarpRequests_.empty()) {
		/* If we have requests support, there must be one available */
		ASSERT(!availableDewarpRequests_.empty());
		dewarpRequest = availableDewarpRequests_.front();
		availableDewarpRequests_.pop();
	}

	bool update = false;
	auto &vertexMap = dewarper_->vertexMap(&data->mainPathStream_);

	const auto &scale = request->controls().get(controls::draft::Dw100Scale);
	if (scale) {
		vertexMap.setScale(*scale);
		update = true;
	}

	const auto &rotation = request->controls().get(controls::draft::Dw100Rotation);
	if (rotation) {
		vertexMap.setRotation(*rotation);
		update = true;
	}

	const auto &offset = request->controls().get(controls::draft::Dw100Offset);
	if (offset) {
		vertexMap.setOffset(*offset);
		update = true;
	}

	const auto &scaleMode = request->controls().get(controls::draft::Dw100ScaleMode);
	if (scaleMode) {
		vertexMap.setMode(static_cast<Dw100VertexMap::ScaleMode>(*scaleMode));
		update = true;
	}

	const auto &lensDewarpEnable = request->controls().get(controls::LensDewarpEnable);
	if (lensDewarpEnable) {
		vertexMap.setLensDewarpEnable(*lensDewarpEnable);
		update = true;
	}

	/* Handle scaler crop control. */
	const auto &crop = request->controls().get(controls::ScalerCrop);
	if (crop) {
		if (!dewarper_->supportsRequests())
			LOG(RkISP1, Error)
				<< "Dynamically setting ScalerCrop requires a "
				   "dw100 driver with requests support";
		vertexMap.setScalerCrop(*crop);
		update = true;
	}

	if (update)
		dewarper_->applyVertexMap(&data->mainPathStream_, dewarpRequest);

	/*
	 * Queue input and output buffers to the dewarper. The output buffers
	 * for the dewarper are the buffers of the request, supplied by the
	 * application.
	 */
	DewarpBufferInfo dewarpInfo{ buffer, reqInfo->request->findBuffer(&data->mainPathStream_) };
	queuedDewarpBuffers_.push_back(dewarpInfo);
	LOG(RkISP1Schedule, Debug) << "Queue dewarper " << dewarpInfo.inputBuffer
				   << " " << dewarpInfo.outputBuffer;
	int ret = dewarper_->queueBuffers(buffer, request->buffers(), dewarpRequest);
	if (ret < 0) {
		LOG(RkISP1, Error) << "Failed to queue buffers to dewarper: -"
				   << strerror(-ret);

		/* Push it back into the queue. */
		if (dewarpRequest)
			dewarpRequestReady(dewarpRequest);

		cancelDewarpRequest(reqInfo->request);

		return;
	}

	if (dewarpRequest) {
		ret = dewarpRequest->queue();
		if (ret < 0) {
			LOG(RkISP1, Error) << "Failed to queue dewarp request: -"
					   << strerror(-ret);
			/* Push it back into the queue. */
			dewarpRequestReady(dewarpRequest);

			cancelDewarpRequest(reqInfo->request);
		}
	}

	auto &meta = request->metadata();
	std::array<float, 2> effectiveScale = vertexMap.effectiveScale();
	meta.set(controls::draft::Dw100EffectiveScale, effectiveScale);
	meta.set(controls::draft::Dw100Scale, (effectiveScale[0] + effectiveScale[1]) / 2.0);
	meta.set(controls::draft::Dw100Rotation, vertexMap.rotation());
	meta.set(controls::draft::Dw100Offset, vertexMap.effectiveOffset());
	meta.set(controls::ScalerCrop, vertexMap.effectiveScalerCrop());

	if (vertexMap.dewarpParamsValid())
		meta.set(controls::LensDewarpEnable, vertexMap.lensDewarpEnable());
}

void PipelineHandlerRkISP1::dewarpRequestReady(V4L2Request *request)
{
	request->reinit();
	availableDewarpRequests_.push(request);
}

void PipelineHandlerRkISP1::dewarpBufferReady(FrameBuffer *buffer)
{
	Request *request = buffer->request();
	const FrameMetadata &metadata = buffer->metadata();

	/*
	 * After stopping the dewarper, the buffers are returned out of order.
	 * Search the list for the corresponding info and handle it. In regular
	 * operation it will always be the first entry.
	 */
	for (DewarpBufferInfo &dwInfo : queuedDewarpBuffers_) {
		if (dwInfo.outputBuffer != buffer)
			continue;

		availableMainPathBuffers_.push(dwInfo.inputBuffer);
		dwInfo.inputBuffer = nullptr;
		dwInfo.outputBuffer = nullptr;

		if (metadata.status == FrameMetadata::FrameCancelled)
			buffer->_d()->cancel();

		completeBuffer(request, buffer);
	}

	while (!queuedDewarpBuffers_.empty() &&
	       queuedDewarpBuffers_.front().inputBuffer == nullptr)
		queuedDewarpBuffers_.pop_front();

	tryCompleteRequests();
	queueInternalBuffers();
}

void PipelineHandlerRkISP1::paramBufferReady(FrameBuffer *buffer)
{
	LOG(RkISP1Schedule, Debug) << "Param buffer ready " << buffer;

	/* After stream off, the buffers are returned out of order, so
	 * we don't care about the rest.
	 */
	if (!running_) {
		availableParamBuffers_.push(buffer);
		return;
	}

	ParamBufferInfo pInfo = queuedParamBuffers_.front();
	queuedParamBuffers_.pop();

	ASSERT(pInfo.buffer == buffer);

	size_t metaSequence = buffer->metadata().sequence;
	LOG(RkISP1Schedule, Debug) << "Params buffer ready "
				   << " Expected: " << pInfo.expectedSequence
				   << " got: " << metaSequence;
	paramsSyncHelper_.gotFrame(pInfo.expectedSequence, metaSequence);
	availableParamBuffers_.push(buffer);
}

void PipelineHandlerRkISP1::statBufferReady(FrameBuffer *buffer)
{
	ASSERT(activeCamera_);
	RkISP1CameraData *data = cameraData(activeCamera_);

	size_t sequence = buffer->metadata().sequence;

	if (buffer->metadata().status == FrameMetadata::FrameCancelled) {
		LOG(RkISP1Schedule, Warning) << "Stats cancelled " << sequence;
		/*
		 * We can't assume that the sequence of the stat buffer is valid,
		 * so there is nothing left to do.
		 */
		availableStatBuffers_.push(buffer);
		return;
	}

	LOG(RkISP1Schedule, Debug) << "Stats ready " << sequence;

	if (nextStatsToProcess_ != sequence)
		LOG(RkISP1Schedule, Warning) << "Stats sequence out of sync."
					     << " Expected: " << nextStatsToProcess_
					     << " got: " << sequence;

	if (nextStatsToProcess_ > sequence) {
		LOG(RkISP1Schedule, Warning) << "Stats were too late. Ignored";
		availableStatBuffers_.push(buffer);
		return;
	}

	/* Send empty stats to ensure metadata gets created*/
	while (nextStatsToProcess_ < sequence) {
		LOG(RkISP1Schedule, Warning) << "Send empty stats to fill metadata";
		data->ipa_->processStats(nextStatsToProcess_, 0,
					 data->delayedCtrls_->get(nextStatsToProcess_));

		nextStatsToProcess_++;
	}

	nextStatsToProcess_++;

	sensorFrameInfos_[sequence].statsBuffer = buffer;

	LOG(RkISP1Schedule, Debug) << "Process stats " << sequence;
	data->ipa_->processStats(sequence, buffer->cookie(),
				 data->delayedCtrls_->get(sequence));
}

REGISTER_PIPELINE_HANDLER(PipelineHandlerRkISP1, "rkisp1")

} /* namespace libcamera */
