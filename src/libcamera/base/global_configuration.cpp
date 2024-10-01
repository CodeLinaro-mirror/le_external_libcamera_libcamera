/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2024 Red Hat, inc.
 *
 * global_configuration.cpp - Global configuration handling
 */

#include "libcamera/internal/global_configuration.h"

#include <filesystem>
#include <memory>
#include <string>
#include <sys/types.h>

#include <libcamera/base/file.h>
#include <libcamera/base/log.h>
#include <libcamera/base/utils.h>

#include "libcamera/internal/yaml_parser.h"

namespace libcamera {

LOG_DEFINE_CATEGORY(Configuration)

std::unique_ptr<GlobalConfiguration> GlobalConfiguration::instance_ =
	std::make_unique<GlobalConfiguration>();

/**
 * \brief Initialize the global configuration
 *
 * This method is expected to be called only once, before the configuration is
 * queried for the first time.
 *
 * \note Most notably, the method must be called before any logging, because
 * logging queries the configuration.
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
 * Example configuration file content:
 * \code{.yaml}
 * configuration:
 *   log:
 *     file: syslog
 *     levels: 'IPAManager:DEBUG'
 * \endcode
 *
 * The configuration file is looked up in user's home directory first and if it
 * is not found then in system-wide configuration directories. If multiple
 * configuration files exist then only the first one found is used and no
 * configuration merging is performed.
 *
 * The class is used as a private singleton accessed by the provided
 * helpers. Namely GlobalConfiguration::option or GlobalConfiguration::envOption
 * to access individual options or GlobalConfiguration::configuration() to
 * access the whole configuration.
 */

/**
 * \typedef GlobalConfiguration::Configuration
 * \brief Type representing global libcamera configuration
 *
 * All code outside GlobalConfiguration must use this type declaration and not
 * the underlying type.
 */

/**
 * \brief Do not create GlobalConfiguration instance directly, use initialize()
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

/*
 * Care is needed here to not log anything before the configuration is
 * loaded otherwise the logger would be initialized with empty configuration.
 */

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
	configuration_ = std::move(root);

	return true;
}

GlobalConfiguration::Configuration GlobalConfiguration::get()
{
	return *instance_->configuration_;
}

/**
 * \brief Return value of the configuration option identified by \a confPath
 * \param[in] confPath Sequence of the YAML section names (excluding
 * `configuration') leading to the requested option separated by dots
 * \return A value if an item corresponding to \a confPath exists in the
 * configuration file, no value otherwise
 */
std::optional<std::string> GlobalConfiguration::option(
	const std::string &confPath)
{
	YamlObject *c = &const_cast<YamlObject &>(configuration());
	for (auto part : utils::details::StringSplitter(confPath, "."))
		if (c->contains(part))
			c = &const_cast<YamlObject &>((*c)[part]);
		else
			return std::optional<std::string>();
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
	const std::string &confPath)
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
