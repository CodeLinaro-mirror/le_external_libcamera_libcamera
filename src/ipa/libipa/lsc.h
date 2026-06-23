/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2026 Ideas on Board Oy
 *
 * libIPA Lsc algorithms
 */

#pragma once

#include <memory>

#include <libcamera/control_ids.h>

#include "interpolator.h"
#include "lsc_grid.h"
#include "lsc_polynomial.h"

namespace libcamera {

LOG_DECLARE_CATEGORY(Lsc)

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

#ifndef __DOXYGEN__
template<typename T>
void interpolateVector(const std::vector<T> &a, const std::vector<T> &b,
		       std::vector<T> &dest, double lambda)
{
	ASSERT(a.size() == b.size());
	dest.resize(a.size());
	for (size_t i = 0; i < a.size(); i++)
		dest[i] = a[i] * (1.0 - lambda) + b[i] * lambda;
}

template<>
void Interpolator<lsc::Components<uint16_t>>::
	interpolate(const lsc::Components<uint16_t> &a,
		    const lsc::Components<uint16_t> &b,
		    lsc::Components<uint16_t> &dest,
		    double lambda);
#endif /* __DOXYGEN__ */

class LscAlgorithmBase
{
public:
	void queueRequest(lsc::ActiveState &state, lsc::FrameContext &context,
			  const ControlList &controls);
	void process(lsc::FrameContext &context, ControlList &metadata);
};

template<typename T, typename U>
class LscAlgorithm : public LscAlgorithmBase
{
public:
	int init(const ValueNode &tuningData, ControlInfoMap::Map &controls,
		 LscDescriptor descriptor)
	{
		polynomial_ = false;

		std::string type = tuningData["type"].get<std::string>("table");
		if (type == "table") {
			impl_ = std::make_unique<LscGrid<T, U>>();
			LOG(Lsc, Debug) << "Using grid-based Lsc";
		} else if (type == "polynomial") {
			impl_ = std::make_unique<LscPolynomial<T, U>>();
			polynomial_ = true;
			LOG(Lsc, Debug) << "Using polynomial Lsc";
		} else {
			LOG(Lsc, Error) << "Unsupported Lsc algorithm '"
					<< type << "'";
			return -EINVAL;
		}

		const ValueNode &yamlSets = tuningData["sets"];
		if (!yamlSets.isList()) {
			LOG(Lsc, Error) << "'sets' parameter not found in tuning file";
			return -EINVAL;
		}

		int ret = impl_->parseLscData(yamlSets, descriptor);
		if (ret)
			return ret;

		controls[&controls::LensShadingCorrectionEnable] =
			ControlInfo(false, true, true);

		return 0;
	}

	int configure(lsc::ActiveState &state, const Rectangle &analogCrop,
		      const std::vector<double> &xPos,
		      const std::vector<double> &yPos)
	{
		LOG(Lsc, Debug) << "Sample Lsc data for " << analogCrop;
		lsc::ComponentsMap<T> lscData =
			impl_->resampleLscData(analogCrop, xPos, yPos);

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

	const lsc::Components<T> interpolateComponents(unsigned int ct)
	{
		return sets_.getInterpolated(ct);
	}

	const lsc::ComponentsMap<T> getComponents()
	{
		return lscData_;
	}

private:
	std::unique_ptr<LscImplementation<T, U>> impl_;
	Interpolator<lsc::Components<T>> sets_;
	lsc::ComponentsMap<T> lscData_;
	bool polynomial_;
};

} /* namespace ipa */

} /* namespace libcamera */
