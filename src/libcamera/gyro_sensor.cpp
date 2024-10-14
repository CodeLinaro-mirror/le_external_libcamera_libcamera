/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2024, Google Inc.
 *
 * gyro_sensor.cpp - A gyroscope sensor
 */

/**
 * \class libcamera::GyroSensor
 * \brief Gyroscope Sensor
 *
 * The GyroSensor class models a gyroscope sensor capable of reporting
 * gyroscope samples, with x, y, z, and timestamp channels. There might be
 * multiple gyroscope sensors on a system, while should be distinguishable via
 * GyroSensor::Location.
 */

/**
 * \enum libcamera::GyroSensor::Location
 * \brief Gyroscope sensor's location type
 * \var GyroSensor::kNone
 * \brief No location attribute available
 * \var GyroSensor::kBase
 * \brief Gyroscope sensor is on the base
 * \var GyroSensor::kLid
 * \brief Gyroscope sensor is on the lid
 * \var GyroSensor::kCamera
 * \brief Gyroscope sensor is on the camera
 */

/**
 * \struct libcamera::GyroSensor::SensorSample
 * \brief SensorSample contains all available channels within a sample
 *
 * \var SensorSample::x_value
 * \brief Sensor reading on the x axis
 *
 * \var SensorSample::y_value
 * \brief Sensor reading on the y axis
 *
 * \var SensorSample::z_value
 * \brief Sensor reading on the z axis
 *
 * \var SensorSample::timestamp
 * \brief The timestamp of the sample
 */

/**
 * \fn int GyroSensor::init(Location location)
 * \brief Initialize with \a location inquiried
 * \param[in] location The gyroscope with \a location needed
 * \return 0 on success or a negative error code otherwise
 */

/**
 * \fn bool GyroSensor::startReading(double frequency)
 * \brief Start reading gyroscope samples with \a frequency
 * \param[in] frequency The frequency of samples required
 *
 * This should be called after init().
 *
 * \sa init()
 *
 * \return True if succeed
 */

/**
 * \fn void GyroSensor::stopReading()
 * \brief Stop reading gyroscope samples
 *
 * \sa startReading()
 */

/**
 * \fn SensorSample GyroSensor::getLatestSample()
 *
 * This should be called after startReading()
 *
 * \sa startReading()
 *
 * \return The latest received sample
 */
