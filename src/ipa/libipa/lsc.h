/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2026 Ideas on Board Oy
 *
 * libIPA Lsc algorithm
 */

#pragma once

#include <memory>
#include <vector>

#include <libcamera/controls.h>
#include <libcamera/geometry.h>

#include "libcamera/internal/value_node.h"

#include "interpolator.h"
#include "lsc_base.h"

namespace libcamera {

namespace ipa {

namespace lsc {

struct ActiveState {
	bool enabled;
};

struct FrameContext {
	bool enabled;
	bool update;
};

} /* namespace lsc */

class LscAlgorithmBase
{
public:
	int init(const ValueNode &tuningData, ControlInfoMap::Map &controls,
		 const LscDescriptor &descriptor);

	void queueRequest(lsc::ActiveState &state, lsc::FrameContext &context,
			  const ControlList &controls);
	void process(lsc::FrameContext &context, ControlList &metadata);

protected:
	LscAlgorithmBase() = default;

	std::unique_ptr<LscImplementation> impl_;
	bool polynomial_;
};

template<typename U>
class LscAlgorithm : public LscAlgorithmBase
{
private:
	using T = typename U::QuantizedType;

	template<typename V>
	class _Components : public std::map<std::string, std::vector<V>>
	{
	};

	template<typename V>
	class _ComponentsMap : public std::map<unsigned int, _Components<V>>
	{
	};

public:
	using Components = _Components<T>;
	using ComponentsMap = _ComponentsMap<T>;

	LscAlgorithm() = default;

	int configure(lsc::ActiveState &state, const Rectangle &analogCrop,
		      const std::vector<double> &xPos,
		      const std::vector<double> &yPos)
	{
		lsc::ComponentsMap data =
			impl_->sampleForCrop(analogCrop, xPos, yPos);

		ComponentsMap lscData;
		for (const auto &[t, c] : data) {
			Components comp;

			for (const auto &[k, gains] : c) {
				std::vector<T> quantizedGains;
				quantizedGains.reserve(gains.size());

				for (const float &gain : gains) {
					if (polynomial_)
						quantizedGains.push_back(U(gain).quantized());
					else
						quantizedGains.push_back(gain);
				}

				comp[k] = std::move(quantizedGains);
			}

			lscData[t] = comp;
		}

		/*
		 * Retain a copy of the components table.
		 *
		 * We could avoid a copy here if getComponents() could
		 * return sets_.data() but I wasn't able to work around the
		 * compiler refusing it.
		 */
		lscData_ = lscData;

		sets_.setData(std::move(lscData));
		state.enabled = true;

		return 0;
	}

	Interpolator<Components> &getInterpolator()
	{
		return sets_;
	}

	const ComponentsMap &getComponents() const
	{
		return lscData_;
	}

private:
	ComponentsMap lscData_;
	Interpolator<Components> sets_;
};

} /* namespace ipa */

} /* namespace libcamera */
