/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2024, Ideas On Board Oy
 *
 * libcamera YAML emitter helper
 */

#include "libcamera/internal/yaml_emitter.h"

#include <libcamera/base/log.h>

/**
 * \file yaml_emitter.h
 * \brief A YAML emitter helper
 *
 * The YAML emitter helpers allow users to emit output in YAML format.
 *
 * To emit YAML users of the these helper classes create a root node with
 *
 * \code
	std::string filePath("...");
	auto root = YamlEmitter::root(filePath);
   \endcode
 *
 * and start populating it with dictionaries and lists with the YamlRoot::dict()
 * and YamlRoot::list() functions.
 *
 * The classes part of this file implement RAII-style handling of YAML
 * events. By creating a YamlList and YamlDict instance the associated YAML
 * sequence start and mapping start events are emitted and once the instances
 * gets destroyed the corresponding sequence end and mapping end events are
 * emitted.
 *
 * From an initialized YamlRoot instance is possible to create YAML list and
 * dictionaries.
 *
 * \code
	YamlDict dict = root.dict();
	YamlList list = root.list();
   \endcode
 *
 * YamlDict instances can be populated with scalars associated with a key
 *
 * \code
	dict["key"] = "value";
   \endcode
 *
 * and it is possible to create lists and dictionaries, associated with a key
 *
 * \code
	YamlDict subDict = dict.dict("newDict");
	YamlList subList = dict.list("newList");
   \endcode
 *
 * YamlList instances can be populated with scalar elements
 *
 * \code
	list.scalar("x");
	list.scalar("y");
   \endcode
 *
 * and with dictionaries and lists too
 *
 * \code
	YamlDict subDict = list.dict();
	YamlList subList = list.list();
   \endcode
 */

namespace libcamera {

LOG_DEFINE_CATEGORY(YamlEmitter)

namespace {

int yamlWrite(void *data, unsigned char *buffer, size_t size)
{
	File *file = static_cast<File *>(data);

	Span<unsigned char> buf{ buffer, size };
	ssize_t ret = file->write(buf);
	if (ret < 0) {
		LOG(YamlEmitter, Error) << "Write error : " << strerror(ret);
		return 0;
	}

	return 1;
}

} /* namespace */

/**
 * \class YamlEmitter
 *
 * YAML helper classes entry point. This class allows to create a YamlRoot
 * instances, using the YamlEmitter::root() function, that users can populate
 * with lists, dictionaries and scalars.
 */

YamlEmitter::YamlEmitter(const std::string &path)
{
	std::string filePath(path);
	file_.setFileName(filePath);
	file_.open(File::OpenModeFlag::WriteOnly);
}

/**
 * \brief Destroy the YamlEmitter
 */
YamlEmitter::~YamlEmitter()
{
	yaml_event_delete(&event_);
	yaml_emitter_delete(&emitter_);
}

/**
 * \brief Create an initialized instance of YamlRoot
 * \param[in] path The YAML output file path
 *
 * Create an initialized instance of the YamlRoot class that users can start
 * using and populating with scalers, lists and dictionaries.
 *
 * \return An initialized YamlRoot instance
 */
YamlRoot YamlEmitter::root(const std::string &path)
{
	std::unique_ptr<YamlEmitter> emitter{ new YamlEmitter(path) };

	emitter->init();

	return YamlRoot(std::move(emitter));
}

void YamlEmitter::logError()
{
	switch (emitter_.error) {
	case YAML_MEMORY_ERROR:
		LOG(YamlEmitter, Error)
			<< "Memory error: Not enough memory for emitting";
		break;

	case YAML_WRITER_ERROR:
		LOG(YamlEmitter, Error)
			<< "Writer error: " << emitter_.problem;
		break;

	case YAML_EMITTER_ERROR:
		LOG(YamlEmitter, Error)
			<< "Emitter error: " << emitter_.problem;
		break;

	default:
		LOG(YamlEmitter, Error) << "Internal problem";
		break;
	}
}

void YamlEmitter::init()
{
	yaml_emitter_initialize(&emitter_);
	yaml_emitter_set_output(&emitter_, yamlWrite, &file_);

	yaml_stream_start_event_initialize(&event_, YAML_UTF8_ENCODING);
	emit();

	yaml_document_start_event_initialize(&event_, NULL, NULL, NULL, 0);
	emit();
}

int YamlEmitter::emit()
{
	int ret = yaml_emitter_emit(&emitter_, &event_);
	if (!ret) {
		logError();
		return -EINVAL;
	}

	return 0;
}

/**
 * \class YamlOutput
 *
 * The YamlOutput base class. From this class are derived the YamlScalar,
 * YamlList and YamlDict classes which are meant to be used by users of the
 * YAML emitter helpers.
 *
 * The YamlOutput base class provides functions to create YAML lists and
 * dictionaries and to populate them.
 *
 * Instances of this class cannot be instantiated directly by applications.
 */

/**
 * \fn YamlOutput::valid()
 * \brief Check if a YamlOutput instance has been correctly initialized
 * \return True if the instance has been initialized, false otherwise
 */

/**
 * \fn YamlOutput::YamlOutput(YamlEmitter *emitter)
 * \brief Create a YamlOutput instance with an associated emitter
 * \param[in] emitter The YAML emitter
 */

/**
 * \fn YamlOutput &YamlOutput::operator=(YamlOutput &&other)
 * \brief The move-assignment operator
 * \param[in] other The instance to be moved
 */

/**
 * \brief Emit \a scalar as a YAML scalar
 * \param[in] scalar The element to emit
 * \return 0 in case of success, a negative error value otherwise
 */
int YamlOutput::emitScalar(std::string_view scalar)
{
	if (!valid())
		return -EINVAL;

	const yaml_char_t *value = reinterpret_cast<const yaml_char_t *>
				   (scalar.data());
	yaml_scalar_event_initialize(&emitter_->event_, NULL, NULL, value,
				     scalar.length(), true, false,
				     YAML_PLAIN_SCALAR_STYLE);
	return emitter_->emit();
}

/**
 * \brief Emit the mapping start YAML event
 * \return 0 in case of success, a negative error value otherwise
 */
int YamlOutput::emitMappingStart()
{
	if (!valid())
		return -EINVAL;

	yaml_mapping_start_event_initialize(&emitter_->event_, NULL, NULL,
					    true, YAML_BLOCK_MAPPING_STYLE);
	return emitter_->emit();
}

/**
 * \brief Emit the mapping end YAML event
 * \return 0 in case of success, a negative error value otherwise
 */
int YamlOutput::emitMappingEnd()
{
	if (!valid())
		return -EINVAL;

	yaml_mapping_end_event_initialize(&emitter_->event_);
	return emitter_->emit();
}

/**
 * \brief Emit the sequence start YAML event
 * \return 0 in case of success, a negative error value otherwise
 */
int YamlOutput::emitSequenceStart()
{
	if (!valid())
		return -EINVAL;

	yaml_sequence_start_event_initialize(&emitter_->event_, NULL, NULL,
					     true, YAML_BLOCK_SEQUENCE_STYLE);
	return emitter_->emit();
}

/**
 * \brief Emit the sequence end YAML event
 * \return 0 in case of success, a negative error value otherwise
 */
int YamlOutput::emitSequenceEnd()
{
	if (!valid())
		return -EINVAL;

	yaml_sequence_end_event_initialize(&emitter_->event_);
	return emitter_->emit();
}

/**
 * \brief Create a scalar instance
 * \return An instance of YamlScalar
 */
YamlScalar YamlOutput::scalar()
{
	return YamlScalar(emitter_);
}

/**
 * \brief Create a dictionary instance
 * \return An instance of YamlDict
 */
YamlDict YamlOutput::dict()
{
	return YamlDict(emitter_);
}

/**
 * \brief Create a list instance
 * \return An instance of YamlList
 */
YamlList YamlOutput::list()
{
	return YamlList(emitter_);
}

/**
 * \var YamlOutput::emitter_
 * \brief The emitter used by this YamlObject to output YAML events
 */

/**
 * \var YamlOutput::event_
 * \brief The YAML event used by this YamlObject
 */

/**
 * \class YamlRoot
 *
 * The YAML root node. A valid YamlRoot instance can only be created using the
 * YamlEmitter::root() function. The typical initialization pattern of users of
 * this class is similar to the one in the following example:
 *
 * \code
	class YamlUser
	{
	public:
		YamlUser();

	private:
		YamlRool root_;
	};

	YamlUser::YamlUser()
	{
		root_ = YamlEmitter::root("/path/to/yaml/file.yml");
	}
   \endcode
 *
 * A YamlRoot element can be populated with list and dictionaries.
 */

/**
 * \fn YamlRoot::YamlRoot()
 * \brief Construct a YamlRoot instance without initializing it
 *
 * A YamlRoot instance can be created in non-initialized state typically to be
 * stored as a class member by the users of this class. In order to start using
 * and populating the YamlRoot instance a valid and initialized instance,
 * created using the YamlEmitter::root() function, has to be move-assigned to
 * the non-initialized instance.
 *
 * \code
	YamlRoot root;

	root = YamlEmitter::root("/path/to/yaml/file.yml");
   \endcode
 */

/**
 * \brief Delete a YamlRoot
 */
YamlRoot::~YamlRoot()
{
	if (!valid())
		return;

	yaml_document_end_event_initialize(&emitter_->event_, 0);
	emitterRoot_->emit();

	yaml_stream_end_event_initialize(&emitter_->event_);
	emitterRoot_->emit();
}

/**
 * \fn YamlRoot &YamlRoot::operator=(YamlRoot &&other)
 * \brief Move assignment operator
 *
 * Move-assign a YamlRoot instance. This function is typically used to assign an
 * initialized instance returned by YamlEmitter::root() to a non-initialized
 * one.
 *
 * \return A reference to this instance of YamlRoot
 */

/**
 * \copydoc YamlOutput::dict()
 */
YamlDict YamlRoot::dict()
{
	int ret = emitMappingStart();
	if (ret)
		return {};

	return YamlOutput::dict();
}

/**
 * \copydoc YamlOutput::list()
 */
YamlList YamlRoot::list()
{
	int ret = emitSequenceStart();
	if (ret)
		return {};

	return YamlOutput::list();
}

/**
 * \class YamlScalar
 *
 * A YamlScalar can be assigned to an std::string_view to emit them as YAML
 * elements.
 */

/**
 * \brief Create a YamlScalar instance
 */
YamlScalar::YamlScalar(YamlEmitter *emitter)
	: YamlOutput(emitter)
{
}

/**
 * \brief Emit \a scalar as a YAML scalar
 * \param[in] scalar The element to emit in the YAML output
 */
void YamlScalar::operator=(std::string_view scalar)
{
	emitScalar(scalar);
}

/**
 * \class YamlList
 *
 * A YamlList can be populated with scalars and allows to create nested lists
 * and dictionaries.
 */

/**
 * \brief Create a YamlList
 */
YamlList::YamlList(YamlEmitter *emitter)
	: YamlOutput(emitter)
{
}

/**
 * \brief Destroy a YamlList instance
 */
YamlList::~YamlList()
{
	emitSequenceEnd();
}

/**
 * \fn YamlList &YamlList::operator=(YamlList &&other)
 * \brief Move-assignment operator
 * \param[inout] other The instance to move
 */

/**
 * \brief Append \a scalar to the list
 * \param[in] scalar The element to append to the list
 */
void YamlList::scalar(std::string_view scalar)
{
	emitScalar(scalar);
}

/**
 * \copydoc YamlOutput::list()
 */
YamlList YamlList::list()
{
	int ret = emitSequenceStart();
	if (ret)
		return {};

	return YamlOutput::list();
}

/**
 * \copydoc YamlOutput::dict()
 */
YamlDict YamlList::dict()
{
	int ret = emitMappingStart();
	if (ret)
		return {};

	return YamlOutput::dict();
}

/**
 * \class YamlDict
 *
 * A YamlDict can be populated with scalars using operator[] and allows to
 * create other lists and dictionaries associated with a key.
 */

/**
 * \fn YamlDict::YamlDict()
 * \brief Create a non-initialized instance of a YamlDict
 */

YamlDict::YamlDict(YamlEmitter *emitter)
	: YamlOutput(emitter)
{
}

/**
 * \brief Destroy a YamlDict instance
 */
YamlDict::~YamlDict()
{
	emitMappingEnd();
}

/**
 * \fn YamlDict &YamlDict::operator=(YamlDict &&other)
 * \brief Move-assignment operator
 * \param[inout] other The instance to move
 */

/**
 * \copydoc YamlOutput::list()
 */
YamlList YamlDict::list(std::string_view key)
{
	int ret = emitScalar(key);
	if (ret)
		return {};

	ret = emitSequenceStart();
	if (ret)
		return {};

	return YamlOutput::list();
}

/**
 * \copydoc YamlOutput::dict()
 */
YamlDict YamlDict::dict(std::string_view key)
{
	int ret = emitScalar(key);
	if (ret)
		return {};

	ret = emitMappingStart();
	if (ret)
		return {};

	return YamlOutput::dict();
}

/**
 * \brief Create a scalar associated with \a key in the dictionary
 * \param[in] key The key associated with the newly created scalar
 * \return A YamlScalar that application can use to output text
 */
YamlScalar YamlDict::operator[](std::string_view key)
{
	int ret = emitScalar(key);
	if (ret)
		return {};

	return YamlOutput::scalar();
}

} /* namespace libcamera */
