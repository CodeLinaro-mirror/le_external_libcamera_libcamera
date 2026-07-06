/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2026, Ideas On Board
 *
 * Table-based Lsc implementation
 */
#pragma once

#include <vector>

#include <libcamera/base/log.h>
#include <libcamera/base/span.h>

#include <libcamera/geometry.h>

#include "libcamera/internal/value_node.h"

#include "lsc_base.h"

namespace libcamera {

LOG_DECLARE_CATEGORY(LscTable)

namespace ipa {

template<typename U>
class LscTable : public LscImplementation<U>
{
private:
	using T = typename U::QuantizedType;

public:
	int parseLscData(const ValueNode &sets,
			 const LscDescriptor &descriptor) override
	{
		for (const auto &set : sets.asList()) {
			uint32_t ct = set["ct"].get<uint32_t>(0);

			int ret = parseLscComponent(set, ct, descriptor);
			if (ret)
				return ret;
		}

		if (lscData_.empty()) {
			LOG(LscTable, Error) << "Failed to load any sets";
			return -EINVAL;
		}

		return 0;
	}

	lsc::ComponentsMap<T>
	resampleLscData([[maybe_unused]] const Rectangle &cropRectangle,
			[[maybe_unused]] std::vector<double> xPos,
			[[maybe_unused]] std::vector<double> yPos) override
	{
		LOG(LscTable, Warning)
			<< "Tabular LSC data doesn't support resampling";
		return lscData_;
	}

private:
	int parseLscComponent(const ValueNode &yamlSet,
			      unsigned int ct, const LscDescriptor &descriptor)
	{
		lsc::Components<T> component;
		for (auto &k : descriptor.keys) {
			auto [it, inserted] = component.emplace(
				std::piecewise_construct,
				std::forward_as_tuple(k.c_str()),
				std::forward_as_tuple(parseTable(yamlSet,
								 k.c_str(),
								 descriptor.numHCells,
								 descriptor.numVCells)));
			if (!inserted || it->second.empty()) {
				LOG(LscTable, Error)
					<< "Set " << k << " for color temperature "
					<< ct << " is missing";
				return -EINVAL;
			}
		}

		auto [it, inserted] = lscData_.emplace(ct, component);
		if (!inserted) {
			LOG(LscTable, Error)
				<< "Multiple sets found for color temperature "
				<< ct;
			return -EINVAL;
		}

		return 0;
	}

	std::vector<T> parseTable(const ValueNode &tuningData,
				  const char *prop, unsigned int numHCells,
				  unsigned int numVCells)
	{
		unsigned int kLscNumSamples = numHCells * numVCells;

		std::vector<T> table =
			tuningData[prop].get<std::vector<T>>().value_or(utils::defopt);
		if (table.size() != kLscNumSamples) {
			LOG(LscTable, Error)
				<< "Invalid '" << prop << "' values: expected "
				<< kLscNumSamples
				<< " elements, got " << table.size();
			return {};
		}

		return table;
	}

	lsc::ComponentsMap<T> lscData_;
};

} /* namespace ipa */

} /* namespace libcamera */
