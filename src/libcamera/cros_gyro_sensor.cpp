/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2024, Google Inc.
 *
 * cros_gyro_sensor.cpp - Chromium OS gyroscope sensor using mojo
 */

#include <libcamera/base/log.h>
#include <libcamera/base/mutex.h>
#include <libcamera/base/thread_annotations.h>

#include "libcamera/internal/gyro_sensor.h"

#include <cros-camera/sensor_hal_client.h>

#include "../android/cros_mojo_token.h"

namespace libcamera {

namespace {

std::string convertErrorTypeToString(cros::SamplesObserver::ErrorType error)
{
	switch (error) {
	case cros::SamplesObserver::ErrorType::MOJO_DISCONNECTED:
		return "MOJO_DISCONNECTED";

	case cros::SamplesObserver::ErrorType::READ_FAILED:
		return "READ_FAILED";

	case cros::SamplesObserver::ErrorType::INVALID_ARGUMENT:
		return "INVALID_ARGUMENT";

	case cros::SamplesObserver::ErrorType::DEVICE_REMOVED:
		return "DEVICE_REMOVED";
	}
}

} /* namespace */

LOG_DEFINE_CATEGORY(CrosGyroSensor)

class GyroSensor::Private : public Extensible::Private,
			    public cros::SamplesObserver
{
	LIBCAMERA_DECLARE_PUBLIC(GyroSensor)

public:
	Private();

	int init(Location location);

	bool startReading(double frequency);
	void stopReading();

	SensorSample getLatestSample();

	void OnSampleUpdated(cros::SamplesObserver::Sample sample) override;
	void OnErrorOccurred(cros::SamplesObserver::ErrorType error) override;

private:
	cros::SensorHalClient *sensorHalClient_;
	cros::SensorHalClient::Location location_;

	libcamera::Mutex gyroSampleMutex_;
	SensorSample gyroSample_ LIBCAMERA_TSA_GUARDED_BY(gyroSampleMutex_);
};

/**
 * \class libcamera::GyroSensor::Private
 * \brief CrOS implementation of GyroSensor
 */
GyroSensor::Private::Private()
{
}

/**
 * \brief Initialize with \a location inquiried
 * \param[in] location The gyroscope with \a location needed
 * \return 0 on success or a negative error code otherwise
 */
int GyroSensor::Private::init(Location location)
{
	sensorHalClient_ = cros::SensorHalClient::GetInstance(gCrosMojoToken);
	location_ = static_cast<cros::SensorHalClient::Location>(location);
	if (sensorHalClient_ &&
	    sensorHalClient_->HasDevice(
		    cros::SensorHalClient::DeviceType::kAnglVel, location_)) {
		return 0;
	}

	return -ENXIO;
}

/**
 * \brief Start reading gyroscope samples with \a frequency
 * \param[in] frequency The frequency of samples required
 *
 * This should be called after init().
 *
 * \sa init()
 *
 * \return True if succeed
 */
bool GyroSensor::Private::startReading(double frequency)
{
	return sensorHalClient_->RegisterSamplesObserver(
		cros::SensorHalClient::DeviceType::kAnglVel,
		location_, frequency, this);
}

/**
 * \brief Stop reading gyroscope samples
 *
 * \sa startReading()
 */
void GyroSensor::Private::stopReading()
{
	sensorHalClient_->UnregisterSamplesObserver(this);
}

/**
 * \brief This should be called after startReading()
 *
 * \sa startReading()
 *
 * \return The latest received sample
 */
GyroSensor::SensorSample GyroSensor::Private::getLatestSample()
{
	MutexLocker lock(gyroSampleMutex_);

	return gyroSample_;
}

/**
 * \brief A sample is updated and pushed to GyroSensor::Private
 * \param[in] sample The sample being updated
 */
void GyroSensor::Private::OnSampleUpdated(cros::SamplesObserver::Sample sample)
{
	MutexLocker lock(gyroSampleMutex_);

	gyroSample_.x_value = sample.x_value;
	gyroSample_.y_value = sample.y_value;
	gyroSample_.z_value = sample.z_value;
	gyroSample_.timestamp = sample.timestamp;
}

/**
 * \brief An error is occurred and pushed to GyroSensor::Private
 * \param[in] error The error occurred
 */
void GyroSensor::Private::OnErrorOccurred(cros::SamplesObserver::ErrorType error)
{
	switch (error) {
	case cros::SamplesObserver::ErrorType::READ_FAILED:
		LOG(CrosGyroSensor, Error) << "SensorHalClient error: "
					   << convertErrorTypeToString(error);
		break;

	case cros::SamplesObserver::ErrorType::MOJO_DISCONNECTED:
	case cros::SamplesObserver::ErrorType::INVALID_ARGUMENT:
	case cros::SamplesObserver::ErrorType::DEVICE_REMOVED:
		LOG(CrosGyroSensor, Error) << "SensorHalClient error: "
					   << convertErrorTypeToString(error)
					   << ", aborting all usages";
		auto *sensorHalClient =
			cros::SensorHalClient::GetInstance(gCrosMojoToken);
		if (sensorHalClient)
			sensorHalClient->UnregisterSamplesObserver(this);

		break;
	}
}

PUBLIC_GYRO_SENSOR_IMPLEMENTATION

} /* namespace libcamera */
