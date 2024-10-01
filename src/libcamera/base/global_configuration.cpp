/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2024 Red Hat, inc.
 *
 * global_configuration.cpp - Global configuration handling
 */

#include "libcamera/internal/global_configuration.h"

#include <filesystem>
#include <memory>
#include <sys/types.h>

#include <libcamera/base/file.h>
#include <libcamera/base/log.h>
#include <libcamera/base/utils.h>

#include "libcamera/internal/yaml_parser.h"

namespace libcamera {

std::unique_ptr<GlobalConfiguration> GlobalConfiguration::instance_ =
	std::make_unique<GlobalConfiguration>();

/**
 * \brief Do not create GlobalConfiguration instance directly, use initialize()
 */
void GlobalConfiguration::initialize()
{
	std::unique_ptr<GlobalConfiguration> instance =
		std::make_unique<GlobalConfiguration>();
	instance->load();
	instance_ = std::move(instance);
}

/**
 * \class GlobalConfiguration
 * \brief Support for global libcamera configuration
 *
 * The configuration file is a YAML file and the configuration itself is stored
 * under `configuration' top-level item.
 *
 * The configuration file is looked up in user's home directory first and if it
 * is not found then in system-wide configuration directories. If multiple
 * configuration files exist then only the first one found is used and no
 * configuration merging is performed.
 *
 * The class is used as a private singleton and the configuration can be
 * accessed using GlobalConfiguration::configuration().
 */

/**
 * \typedef GlobalConfiguration::Configuration
 * \brief Type representing global libcamera configuration
 *
 * All code outside GlobalConfiguration must use this type declaration and not
 * the underlying type.
 */

GlobalConfiguration::GlobalConfiguration()
	: configuration_(std::make_unique<YamlObject>())
{
}

const std::vector<std::filesystem::path>
	GlobalConfiguration::globalConfigurationFiles = {
		std::filesystem::path(LIBCAMERA_SYSCONF_DIR) / "configuration.yaml",
		std::filesystem::path("/etc/libcamera/configuration.yaml"),
	};

void GlobalConfiguration::load()
{
	std::filesystem::path userConfigurationDirectory;
	char *xdgConfigHome = utils::secure_getenv("XDG_CONFIG_HOME");
	if (xdgConfigHome) {
		userConfigurationDirectory = xdgConfigHome;
	} else {
		const char *home = utils::secure_getenv("HOME");
		if (home)
			userConfigurationDirectory =
				std::filesystem::path(home) / ".config";
	}

	if (!userConfigurationDirectory.empty()) {
		std::filesystem::path user_configuration_file =
			userConfigurationDirectory / "libcamera" / "configuration.yaml";
		if (loadFile(user_configuration_file))
			return;
	}

	for (auto path : globalConfigurationFiles)
		if (loadFile(path))
			return;
}

bool GlobalConfiguration::loadFile(const std::filesystem::path &fileName)
{
	File file(fileName);
	if (!file.exists()) {
		return false;
	}

	if (!file.open(File::OpenModeFlag::ReadOnly))
		return true;

	auto root = YamlParser::parse(file);
	if (!root)
		return true;
	configuration_ = std::move(root);

	return true;
}

GlobalConfiguration::Configuration GlobalConfiguration::get()
{
	return *instance_->configuration_;
}

/**
 * \brief Return configuration version
 *
 * The version is (optionally) declared in the configuration file in the
 * top-level section `version', alongside `configuration'. This has currently no
 * real use but may be needed in future if configuration incompatibilities
 * occur.
 *
 * \return Configuration version as declared in the configuration file or 0 if
 * no version is declared there
 */
unsigned int GlobalConfiguration::version()
{
	return get()["version"].get<unsigned int>().value_or(0);
}

/**
 * \brief Return libcamera global configuration
 *
 * This returns the whole configuration stored in the top-level section
 * `configuration' of the YAML configuration file.
 *
 * The requested part of the configuration can be accessed using \a YamlObject
 * methods.
 *
 * \note \a YamlObject type itself shouldn't be used in type declarations to
 * avoid trouble if we decide to change the underlying data objects in future.
 *
 * \return The whole configuration section
 */
GlobalConfiguration::Configuration GlobalConfiguration::configuration()
{
	return get()["configuration"];
}

} /* namespace libcamera */
