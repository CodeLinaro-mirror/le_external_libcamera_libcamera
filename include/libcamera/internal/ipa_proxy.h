/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2019, Google Inc.
 *
 * Image Processing Algorithm proxy
 */

#pragma once

#include <string>
#include <string_view>

#include <libcamera/ipa/ipa_interface.h>

namespace libcamera {

class IPAModule;

class IPAProxy : public IPAInterface
{
public:
	enum ProxyState {
		ProxyStopped,
		ProxyStopping,
		ProxyRunning,
	};

	IPAProxy(IPAModule *ipam);
	~IPAProxy();

	bool isValid() const { return valid_; }

	std::string configurationFile(std::string_view name,
				      std::string_view fallbackName = {}) const;

protected:
	std::string resolvePath(std::string_view file) const;

	bool valid_;
	ProxyState state_;

private:
	IPAModule *ipam_;
};

} /* namespace libcamera */
