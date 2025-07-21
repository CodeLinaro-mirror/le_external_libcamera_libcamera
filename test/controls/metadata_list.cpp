/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2025, Ideas On Board Oy
 *
 * MetadataList tests
 */

#include <future>
#include <iostream>
#include <thread>

#include <libcamera/control_ids.h>
#include <libcamera/metadata_list.h>
#include <libcamera/property_ids.h>

#include "test.h"

using namespace std;
using namespace libcamera;

#define ASSERT(x) do { \
	if (!static_cast<bool>(x)) { \
		std::cerr << '`' << #x << "` failed" << std::endl; \
		return TestFail; \
	} \
} while (false)

class MetadataListTest : public Test
{
public:
	MetadataListTest() = default;

protected:
	int run() override
	{
		MetadataListPlan mlp;
		mlp.set(controls::ExposureTime);
		mlp.set(controls::ExposureValue);
		mlp.set(controls::ColourGains);
		mlp.set(controls::AfWindows, 10);
		mlp.set(controls::AeEnable);
		mlp.set(controls::SensorTimestamp);

		MetadataList ml(mlp);

		static_assert(static_cast<unsigned int>(properties::LOCATION) == controls::AE_ENABLE);
		ASSERT(ml.set(properties::Location, properties::CameraLocationFront) == MetadataList::SetError::TypeMismatch);

		ASSERT(ml.set(controls::AfWindows, std::array<Rectangle, 11>{}) == MetadataList::SetError::SizeMismatch);
		ASSERT(ml.set(controls::ColourTemperature, 123) == MetadataList::SetError::UnknownTag);

		auto f1 = std::async(std::launch::async, [&] {
			using namespace std::chrono_literals;

			std::this_thread::sleep_for(500ms);
			ASSERT(ml.set(controls::ExposureTime, 0x1111) == MetadataList::SetError());

			std::this_thread::sleep_for(500ms);
			ASSERT(ml.set(controls::ExposureValue, 1) == MetadataList::SetError());

			std::this_thread::sleep_for(500ms);
			ASSERT(ml.set(controls::ColourGains, std::array{
				123.f,
				456.f
			}) == MetadataList::SetError());

			std::this_thread::sleep_for(500ms);
			ASSERT(ml.set(controls::AfWindows, std::array{
				Rectangle(),
				Rectangle(1, 2, 3, 4),
				Rectangle(0x1111, 0x2222, 0x3333, 0x4444),
			}) == MetadataList::SetError());

			return TestPass;
		});

		auto f2 = std::async(std::launch::async, [&] {
			for (;;) {
				const auto x = ml.get(controls::ExposureTime);
				const auto y = ml.get(controls::ExposureValue);
				const auto z = ml.get(controls::ColourGains);
				const auto w = ml.get(controls::AfWindows);

				if (x)
					ASSERT(*x == 0x1111);

				if (y)
					ASSERT(*y == 1.0f);

				if (z) {
					ASSERT(z->size() == 2);
					ASSERT((*z)[0] == 123.f);
					ASSERT((*z)[1] == 456.f);
				}

				if (w) {
					ASSERT(w->size() == 3);
					ASSERT((*w)[0].isNull());
					ASSERT((*w)[1] == Rectangle(1, 2, 3, 4));
					ASSERT((*w)[2] == Rectangle(0x1111, 0x2222, 0x3333, 0x4444));
				}

				if (x && y && z && w)
					break;
			}

			return TestPass;
		});

		ASSERT(f1.get() == TestPass);
		ASSERT(f2.get() == TestPass);

		ASSERT(ml.set(controls::ExposureTime, 0x2222) == MetadataList::SetError::AlreadySet);
		ASSERT(ml.set(controls::ExposureValue, 2) == MetadataList::SetError::AlreadySet);

		ASSERT(ml.get(controls::ExposureTime) == 0x1111);
		ASSERT(ml.get(controls::ExposureValue) == 1);

		for (auto &&[tag, v] : ml)
			std::cout << "[" << tag << "] -> " << v << '\n';

		std::cout << std::endl;

		ml.clear();
		ASSERT(ml.empty());
		ASSERT(ml.size() == 0);

		ASSERT(ml.set(controls::ExposureTime, 0x2222) == MetadataList::SetError());
		ASSERT(ml.get(controls::ExposureTime) == 0x2222);

		auto c = ml.checkpoint();

		ASSERT(ml.set(controls::ExposureValue, 2) == MetadataList::SetError());
		ASSERT(ml.set(controls::SensorTimestamp, 0x99999999) == MetadataList::SetError());

		auto d = c.diffSince();
		ASSERT(&d.list() == &ml);

		ASSERT(ml.set(controls::ColourGains, std::array{ 1.f, 2.f }) == MetadataList::SetError());

		ASSERT(d);
		ASSERT(!d.empty());
		ASSERT(d.size() == 2);
		ASSERT(!d.get(controls::ExposureTime));
		ASSERT(!d.get(controls::ColourGains));
		ASSERT(!d.get(controls::AfWindows));
		ASSERT(d.get(controls::ExposureValue) == 2);
		ASSERT(d.get(controls::SensorTimestamp) == 0x99999999);

		for (auto &&[tag, v] : d)
			std::cout << "[" << tag << "] -> " << v << '\n';

		/* Test if iterators work with algorithms. */
		std::ignore = std::find_if(d.begin(), d.end(), [](const auto &) {
			return false;
		});

#if 0
               {
                       auto it = ml.begin();
                       ml.clear();
                       std::ignore = *it; /* Trigger ASAN. */
               }
#endif

		return TestPass;
	}
};

TEST_REGISTER(MetadataListTest)
