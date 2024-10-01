/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2024 Red Hat, inc.
 *
 * global_configuration.h - Global configuration handling
 */

#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <libcamera/base/utils.h>

#include "libcamera/internal/yaml_parser.h"

namespace libcamera {

class GlobalConfiguration
{
public:
	using Configuration = const YamlObject &;

	/* The constructor must be public to be able to use unique_ptr. */
	GlobalConfiguration();
	static void initialize();

	static unsigned int version();
	static Configuration configuration();

	template<typename T,
		 std::enable_if_t<
			 std::is_same_v<bool, T> ||
			 std::is_same_v<double, T> ||
			 std::is_same_v<int8_t, T> ||
			 std::is_same_v<uint8_t, T> ||
			 std::is_same_v<int16_t, T> ||
			 std::is_same_v<uint16_t, T> ||
			 std::is_same_v<int32_t, T> ||
			 std::is_same_v<uint32_t, T> ||
			 std::is_same_v<std::string, T> ||
			 std::is_same_v<Size, T>> * = nullptr>
	static std::optional<T> option(const std::string &confPath)
	{
		YamlObject *c = &const_cast<YamlObject &>(configuration());
		for (auto part : utils::details::StringSplitter(confPath, "."))
			if (c->contains(part))
				c = &const_cast<YamlObject &>((*c)[part]);
			else
				return std::optional<T>();
		return c->get<T>();
	}

	static std::optional<std::string> envOption(const char *const envVariable,
						    const std::string &confPath);

private:
	static const std::vector<std::filesystem::path> globalConfigurationFiles;

	static std::unique_ptr<GlobalConfiguration> instance_;

	std::unique_ptr<YamlObject> configuration_;

	bool loadFile(const std::filesystem::path &fileName);
	void load();

	static Configuration get();
};

} /* namespace libcamera */
