/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2019, Google Inc.
 *
 * Image Processing Algorithm module manager
 */

#include "libcamera/internal/ipa_manager.h"

#include <algorithm>
#include <functional>
#include <string.h>

#include <libcamera/base/file.h>
#include <libcamera/base/log.h>
#include <libcamera/base/utils.h>

#include "libcamera/internal/ipa_module.h"
#include "libcamera/internal/ipa_proxy.h"
#include "libcamera/internal/pipeline_handler.h"
#include "libcamera/internal/utils.h"

/**
 * \file ipa_manager.h
 * \brief Image Processing Algorithm module manager
 */

namespace libcamera {

LOG_DEFINE_CATEGORY(IPAManager)

/**
 * \class IPAManager
 * \brief Manager for IPA modules
 *
 * The IPA module manager discovers IPA modules from disk, queries and loads
 * them, and creates IPA contexts. It supports isolation of the modules in a
 * separate process with IPC communication and offers a unified IPAInterface
 * view of the IPA contexts to pipeline handlers regardless of whether the
 * modules are isolated or loaded in the same process.
 *
 * Module isolation is based on the module licence. Open-source modules are
 * loaded without isolation, while closed-source module are forcefully isolated.
 * The isolation mechanism ensures that no code from a closed-source module is
 * ever run in the libcamera process.
 *
 * To create an IPA context, pipeline handlers call the IPAManager::createIPA()
 * function. For a directly loaded module, the manager calls the module's
 * ipaCreate() function directly and wraps the returned context in an
 * IPAContextWrapper that exposes an IPAInterface.
 *
 * ~~~~
 * +---------------+
 * |   Pipeline    |
 * |    Handler    |
 * +---------------+
 *         |
 *         v
 * +---------------+                   +---------------+
 * |      IPA      |                   |  Open Source  |
 * |   Interface   |                   |  IPA Module   |
 * | - - - - - - - |                   | - - - - - - - |
 * |  IPA Context  |  ipa_context_ops  |  ipa_context  |
 * |    Wrapper    | ----------------> |               |
 * +---------------+                   +---------------+
 * ~~~~
 *
 * For an isolated module, the manager instantiates an IPAProxy which spawns a
 * new process for an IPA proxy worker. The worker loads the IPA module and
 * creates the IPA context. The IPAProxy alse exposes an IPAInterface.
 *
 * ~~~~
 * +---------------+                   +---------------+
 * |   Pipeline    |                   | Closed Source |
 * |    Handler    |                   |  IPA Module   |
 * +---------------+                   | - - - - - - - |
 *         |                           |  ipa_context  |
 *         v                           |               |
 * +---------------+                   +---------------+
 * |      IPA      |           ipa_context_ops ^
 * |   Interface   |                           |
 * | - - - - - - - |                   +---------------+
 * |   IPA Proxy   |     operations    |   IPA Proxy   |
 * |               | ----------------> |    Worker     |
 * +---------------+      over IPC     +---------------+
 * ~~~~
 *
 * The IPAInterface implemented by the IPAContextWrapper or IPAProxy is
 * returned to the pipeline handler, and all interactions with the IPA context
 * go the same interface regardless of process isolation.
 *
 * In all cases the data passed to the IPAInterface member functions is
 * serialized to Plain Old Data, either for the purpose of passing it to the IPA
 * context plain C API, or to transmit the data to the isolated process through
 * IPC.
 */

/**
 * \brief Construct an IPAManager instance
 *
 * The IPAManager class is meant to only be instantiated once, by the
 * CameraManager.
 */
IPAManager::IPAManager()
{
#if HAVE_IPA_PUBKEY
	if (!pubKey_.isValid())
		LOG(IPAManager, Warning) << "Public key not valid";
#endif

	unsigned int ipaCount = 0;

	auto &modules = modules_;
	std::function<int(const std::string &)> soHandler =
	[&modules](const std::string &file) {
		auto ipaModule = std::make_unique<IPAModule>(file);
		if (!ipaModule->isValid())
			return 0;

		LOG(IPAManager, Debug) << "Loaded IPA module '" << file << "'";

		modules.push_back(std::move(ipaModule));

		return 1;
	};

	/* User-specified paths take precedence. */
	const char *modulePaths = utils::secure_getenv("LIBCAMERA_IPA_MODULE_PATH");
	if (modulePaths) {
		for (const auto &dir : utils::split(modulePaths, ":")) {
			if (dir.empty())
				continue;

			ipaCount += utils::findSharedObjects(dir.c_str(), 0, soHandler);
		}

		if (!ipaCount)
			LOG(IPAManager, Warning)
				<< "No IPA found in '" << modulePaths << "'";
	}

	/*
	 * When libcamera is used before it is installed, load IPAs from the
	 * same build directory as the libcamera library itself.
	 */
	std::string root = utils::libcameraBuildPath();
	if (!root.empty()) {
		std::string ipaBuildPath = root + "src/ipa";
		constexpr int maxDepth = 2;

		LOG(IPAManager, Info)
			<< "libcamera is not installed. Adding '"
			<< ipaBuildPath << "' to the IPA search path";

		ipaCount += utils::findSharedObjects(ipaBuildPath.c_str(), maxDepth, soHandler);
	}

	/* Finally try to load IPAs from the installed system path. */
	ipaCount += utils::findSharedObjects(IPA_MODULE_DIR, 0, soHandler);

	if (!ipaCount)
		LOG(IPAManager, Warning)
			<< "No IPA found in '" IPA_MODULE_DIR "'";
}

IPAManager::~IPAManager() = default;

/**
 * \brief Retrieve an IPA module that matches a given pipeline handler
 * \param[in] pipe The pipeline handler
 * \param[in] minVersion Minimum acceptable version of IPA module
 * \param[in] maxVersion Maximum acceptable version of IPA module
 */
IPAModule *IPAManager::module(PipelineHandler *pipe, uint32_t minVersion,
			      uint32_t maxVersion)
{
	for (const auto &module : modules_) {
		if (module->match(pipe, minVersion, maxVersion))
			return module.get();
	}

	return nullptr;
}

/**
 * \fn IPAManager::createIPA()
 * \brief Create an IPA proxy that matches a given pipeline handler
 * \param[in] pipe The pipeline handler that wants a matching IPA proxy
 * \param[in] minVersion Minimum acceptable version of IPA module
 * \param[in] maxVersion Maximum acceptable version of IPA module
 *
 * \return A newly created IPA proxy, or nullptr if no matching IPA module is
 * found or if the IPA proxy fails to initialize
 */

#if HAVE_IPA_PUBKEY
/**
 * \fn IPAManager::pubKey()
 * \brief Retrieve the IPA module signing public key
 *
 * IPA module signature verification is normally handled internally by the
 * IPAManager class. This function is meant to be used by utilities that need to
 * verify signatures externally.
 *
 * \return The IPA module signing public key
 */
#endif

bool IPAManager::isSignatureValid([[maybe_unused]] IPAModule *ipa) const
{
#if HAVE_IPA_PUBKEY
	char *force = utils::secure_getenv("LIBCAMERA_IPA_FORCE_ISOLATION");
	if (force && force[0] != '\0') {
		LOG(IPAManager, Debug)
			<< "Isolation of IPA module " << ipa->path()
			<< " forced through environment variable";
		return false;
	}

	File file{ ipa->path() };
	if (!file.open(File::OpenModeFlag::ReadOnly))
		return false;

	Span<uint8_t> data = file.map();
	if (data.empty())
		return false;

	bool valid = pubKey_.verify(data, ipa->signature());

	LOG(IPAManager, Debug)
		<< "IPA module " << ipa->path() << " signature is "
		<< (valid ? "valid" : "not valid");

	return valid;
#else
	return false;
#endif
}

} /* namespace libcamera */
