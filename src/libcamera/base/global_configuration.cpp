/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2024-2025 Red Hat, inc.
 *
 * Global configuration handling
 */

#include "libcamera/internal/global_configuration.h"

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <sys/types.h>

#include <libcamera/base/file.h>
#include <libcamera/base/log.h>
#include <libcamera/base/utils.h>

#include "libcamera/internal/yaml_parser.h"

namespace libcamera {

namespace {
const std::vector<std::filesystem::path>
	globalConfigurationFiles = {
		std::filesystem::path(LIBCAMERA_SYSCONF_DIR) / "configuration.yaml",
		std::filesystem::path("/etc/libcamera/configuration.yaml"),
	};
}

LOG_DEFINE_CATEGORY(Configuration)

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
 * The configuration can be accessed using the provided helpers. Namely
 * GlobalConfiguration::option() or GlobalConfiguration::envOption() to access
 * individual options or GlobalConfiguration::configuration() to access the
 * whole configuration.
 */

bool GlobalConfiguration::loadFile(const std::filesystem::path &fileName)
{
	File file(fileName);
	if (!file.exists()) {
		return false;
	}

	if (!file.open(File::OpenModeFlag::ReadOnly)) {
		LOG(Configuration, Warning)
			<< "Failed to open configuration file " << fileName;
		return true;
	}

	auto root = YamlParser::parse(file);
	if (!root) {
		LOG(Configuration, Warning)
			<< "Failed to parse configuration file " << fileName;
		return true;
	}
	yamlConfiguration = std::move(root);

	return true;
}

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

	for (const auto &path : globalConfigurationFiles)
		if (loadFile(path))
			return;
}

GlobalConfiguration::Configuration GlobalConfiguration::get() const
{
	return *yamlConfiguration;
}

/**
 * \brief Initialize the global configuration
 */
GlobalConfiguration::GlobalConfiguration()
{
	load();
}

/**
 * \typedef GlobalConfiguration::Configuration
 * \brief Type representing global libcamera configuration
 *
 * All code outside GlobalConfiguration must use this type declaration and not
 * the underlying type.
 */

/**
 * \brief Return value of the configuration option identified by \a confPath
 * \param[in] confPath Sequence of the YAML section names (excluding
 * `configuration') leading to the requested option separated by dots
 * \return A value if an item corresponding to \a confPath exists in the
 * configuration file, no value otherwise
 */
std::optional<std::string> GlobalConfiguration::option(const std::string &confPath) const
{
	const YamlObject *c = &configuration();
	for (auto part : utils::split(confPath, ".")) {
		c = &(*c)[part];
		if (!*c)
			return {};
	}
	return c->get<std::string>();
}

/**
 * \brief Return value of the configuration option from a file or environment
 * \param[in] envVariable Environment variable to get the value from
 * \param[in] confPath The same as in GlobalConfiguration::option
 *
 * This helper looks first at the given environment variable and if it is
 * defined then it returns its value (even if it is empty). Otherwise it looks
 * for \a confPath the same way as in GlobalConfiguration::option. Only string
 * values are supported.
 *
 * \note Support for using environment variables to configure libcamera behavior
 * is provided here mostly for backward compatibility reasons. Introducing new
 * configuration environment variables is discouraged.
 *
 * \return A value retrieved from the given environment option or configuration
 * file or no value if not found
 */
std::optional<std::string> GlobalConfiguration::envOption(
	const char *const envVariable,
	const std::string &confPath) const
{
	const char *envValue = utils::secure_getenv(envVariable);
	if (envValue)
		return std::optional{ std::string{ envValue } };
	return option(confPath);
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
unsigned int GlobalConfiguration::version() const
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
GlobalConfiguration::Configuration GlobalConfiguration::configuration() const
{
	return get()["configuration"];
}

} /* namespace libcamera */
