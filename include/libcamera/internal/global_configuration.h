/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2024 Red Hat, inc.
 *
 * global_configuration.h - Global configuration handling
 */

#pragma once

#include <filesystem>
#include <vector>

#include "libcamera/internal/yaml_parser.h"

namespace libcamera {

class GlobalConfiguration
{
public:
	using Configuration = const YamlObject &;

	static unsigned int version();
	static Configuration configuration();

private:
	static const std::vector<std::filesystem::path> globalConfigurationFiles;

	bool initialized_;
	std::unique_ptr<YamlObject> configuration_;

	GlobalConfiguration();
	bool loadFile(const std::filesystem::path &fileName);
	void load();
	static const GlobalConfiguration &instance();
	static Configuration get();
};

} /* namespace libcamera */
