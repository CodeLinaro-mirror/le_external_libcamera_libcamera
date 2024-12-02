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

 * Unlike YAML writers that operate on a fully populated representation of the
 * data, the YAML emitter outputs YAML data on the fly. It is suitable for
 * outputting large amount of data with low overhead and no runtime heap
 * allocation.
 *
 * To emit YAML users of the these helper classes create a root node with
 *
 * \code
	std::string filePath("...");
	auto root = YamlEmitter::root(filePath);
   \endcode
 *
 * and start emitting dictionaries and lists with the YamlRoot::dict() and
 * YamlRoot::list() functions.
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
		LOG(YamlEmitter, Error) << "Write error: " << strerror(ret);
		return 0;
	}

	return 1;
}

} /* namespace */

/**
 * \class YamlEmitter
 *
 * YAML helper classes entry point. This class allows to create a YamlRoot
 * instance, using the YamlEmitter::root() function, that users can populate
 * with lists, dictionaries and scalars.
 */

YamlEmitter::YamlEmitter(const std::string &path)
{
	file_.setFileName(path);
	file_.open(File::OpenModeFlag::WriteOnly);
}

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
 * using and populating with scalars, lists and dictionaries.
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
		LOG(YamlEmitter, Error) << "Internal error";
		break;
	}
}

void YamlEmitter::init()
{
	yaml_emitter_initialize(&emitter_);
	yaml_emitter_set_output(&emitter_, yamlWrite, &file_);

	yaml_stream_start_event_initialize(&event_, YAML_UTF8_ENCODING);
	emit();

	yaml_document_start_event_initialize(&event_, nullptr, nullptr,
					     nullptr, 0);
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
 * The YamlOutput base class. From this class are derived the YamlList and
 * YamlDict classes which are meant to be used by users of the YAML emitter
 * helpers.
 *
 * The YamlOutput base class provides functions to create YAML lists and
 * dictionaries and to populate them.
 *
 * This class cannot be instantiated directly.
 */

/**
 * \fn YamlOutput::YamlOutput()
 * \brief The default constructor
 *
 * Create an empty, non-initialized instance. The newly created instance cannot
 * be used without being first move-assigned to a valid and initialized
 * instance. Valid instances of this class can only be created using the
 * YamlRoot::list(), YamlRoot::dict(), YamlList::list(), YamlList::dict(),
 * YamlDict::list() and YamlDict::dict() functions.
 */

/**
 * \brief Create a YamlOutput instance with an associated \a emitter and \a parent
 * \param[in] emitter The YAML emitter
 * \param[in] parent Pointer to the parent node
 *
 * Create a YamlOutput with a valid YamlEmitter and with a reference to the
 * parent node. The parent's child_ pointer is assigned to the newly created
 * instance. If the parent already has a valid child_ reference, unlink and
 * invalidate the chain of previously assigned children nodes by resetting their
 * parent_ pointer. None of the children nodes can be used anymore from this
 * point on.
 */
YamlOutput::YamlOutput(YamlEmitter *emitter, YamlOutput *parent)
	: emitter_(emitter), parent_(parent)
{
	if (!parent_)
		return;

	unlinkChildren(parent_->child_);
	parent_->child_ = this;
}

/**
 * \brief Move constructor
 * \param[in] other The instance to be moved
 *
 * Move an instance and invalidate \a other. The move constructor reassigns
 * the parent's child_ to point to the newly initialized instance.
 */
YamlOutput::YamlOutput(YamlOutput &&other)
{
	emitter_ = other.emitter_;
	event_ = other.event_;
	parent_ = other.parent_;
	child_ = other.child_;

	other.emitter_ = nullptr;
	other.event_ = {};
	other.parent_ = nullptr;
	other.child_ = nullptr;

	if (parent_)
		parent_->child_ = this;
}

YamlOutput::~YamlOutput()
{
	if (parent_)
		parent_->child_ = nullptr;
}

void YamlOutput::unlinkChildren(YamlOutput *node)
{
	YamlOutput *nextNode = node;
	while (nextNode) {
		YamlOutput *nextChild = nextNode->child_;

		nextNode->parent_ = nullptr;
		nextNode->child_ = nullptr;
		nextNode = nextChild;
	}
}

/**
 * \brief Move assignment operator
 * \param[in] other The instance to be moved
 *
 * Move-assign an instance and invalidate \a other. The move assignment operator
 * reassigns the parent's child_ to point to the newly initialized instance.
 */
YamlOutput &YamlOutput::operator=(YamlOutput &&other)
{
	emitter_ = other.emitter_;
	event_ = other.event_;
	parent_ = other.parent_;
	child_ = other.child_;

	other.emitter_ = nullptr;
	other.event_ = {};
	other.parent_ = nullptr;
	other.child_ = nullptr;

	if (parent_)
		parent_->child_ = this;

	return *this;
}

/**
 * \fn YamlOutput::valid()
 * \brief Check if a YamlOutput instance has been correctly initialized
 * \return True if the instance has been initialized, false otherwise
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

	/*
	 * \todo Remove the const_cast<yaml_char_t> of 'value' when
	 * dropping support for libyaml versions < 0.2.3 (shipped in Debian11).
	 *
	 * https://github.com/yaml/libyaml/pull/140
	 */
	yaml_scalar_event_initialize(&emitter_->event_, nullptr, nullptr,
				     const_cast<yaml_char_t *>(value),
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

	yaml_mapping_start_event_initialize(&emitter_->event_, nullptr, nullptr,
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

	yaml_sequence_start_event_initialize(&emitter_->event_, nullptr, nullptr,
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
 * \brief Create a dictionary instance
 * \return An instance of YamlDict
 */
YamlDict YamlOutput::dict(YamlOutput *parent)
{
	return YamlDict(emitter_, parent);
}

/**
 * \brief Create a list instance
 * \return An instance of YamlList
 */
YamlList YamlOutput::list(YamlOutput *parent)
{
	return YamlList(emitter_, parent);
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
 * \var YamlOutput::parent_
 * \brief The parent node, set at object creation time
 *
 * The parent_ class member references the parent node. Only nodes with a valid
 * parent can emit valid YAML output. The parent_ pointer is reset to an invalid
 * value when a new child is created on the parent node, effectively
 * invalidating the current instance.
 */

/**
 * \var YamlOutput::child_
 * \brief The child node
 *
 * The child_ class member references any eventual child created from the
 * current instance using the list() and dict() functions. Only a single
 * valid child_ at the time can be assigned to a YamlOutput instance, if
 * multiple child are created the last created ones overwrites the child_
 * pointer and invalidates all other children.
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
 * stored as a class member to maintain their lifetime active. In order to start
 * using and populating a YamlRoot, a valid and initialized instance created
 * using the YamlEmitter::root() function has to be move-assigned to a
 * non-initialized instance.
 *
 * \code
	YamlRoot root;

	root = YamlEmitter::root("/path/to/yaml/file.yml");
   \endcode
 */

/**
 * \fn YamlRoot::YamlRoot()
 * \copydoc YamlOutput::YamlOutput()
 */

/**
 * \fn YamlRoot &YamlRoot::YamlRoot(YamlRoot &&other)
 * \copydoc YamlOutput &YamlOutput::YamlOutput(YamlOutput &&other)
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
 * \copydoc YamlOutput &operator=(YamlOutput &&other)
 */

/**
 * \copydoc YamlOutput::dict()
 */
YamlDict YamlRoot::dict()
{
	int ret = emitMappingStart();
	if (ret)
		return {};

	return YamlOutput::dict(this);
}

/**
 * \copydoc YamlOutput::list()
 */
YamlList YamlRoot::list()
{
	int ret = emitSequenceStart();
	if (ret)
		return {};

	return YamlOutput::list(this);
}

/**
 * \class YamlList
 *
 * A YamlList can be populated with scalars and allows to create nested lists
 * and dictionaries.
 */

/**
 * \fn YamlList::YamlList()
 * \copydoc YamlOutput::YamlOutput()
 */

/**
 * \copydoc YamlOutput::YamlOutput(YamlEmitter *emitter, YamlOutput *parent)
 */
YamlList::YamlList(YamlEmitter *emitter, YamlOutput *parent)
	: YamlOutput(emitter, parent)
{
}

/**
 * \fn YamlList &YamlList::YamlList(YamlList &&other)
 * \copydoc YamlOutput &YamlOutput::YamlOutput(YamlOutput &&other)
 */

YamlList::~YamlList()
{
	emitSequenceEnd();
}

/**
 * \fn YamlList &YamlList::operator=(YamlList &&other)
 * \copydoc YamlOutput &operator=(YamlOutput &&other)
 */

/**
 * \copydoc YamlOutput::list()
 */
YamlList YamlList::list()
{
	if (!parent_) {
		LOG(YamlEmitter, Error)
			<< "Invalid usage of the YamlEmitter API. "
			<< " The YAML output might not be correct.";
		return {};
	}

	int ret = emitSequenceStart();
	if (ret)
		return {};

	return YamlOutput::list(this);
}

/**
 * \copydoc YamlOutput::dict()
 */
YamlDict YamlList::dict()
{
	if (!parent_) {
		LOG(YamlEmitter, Error)
			<< "Invalid usage of the YamlEmitter API. "
			<< " The YAML output might not be correct.";
		return {};
	}

	int ret = emitMappingStart();
	if (ret)
		return {};

	return YamlOutput::dict(this);
}

/**
 * \brief Append \a scalar to the list
 * \param[in] scalar The element to append to the list
 */
void YamlList::scalar(std::string_view scalar)
{
	if (!parent_) {
		LOG(YamlEmitter, Error)
			<< "Invalid usage of the YamlEmitter API. "
			<< " The YAML output might not be correct.";
		return;
	}

	emitScalar(scalar);
}

/**
 * \class YamlDict
 *
 * A YamlDict can create lists, other dictionaries and emit a scalar associated
 * with a key.
 */

/**
 * \fn YamlDict::YamlDict()
 * \copydoc YamlOutput::YamlOutput()
 */

/**
 * \copydoc YamlOutput::YamlOutput(YamlEmitter *emitter, YamlOutput *parent)
 */
YamlDict::YamlDict(YamlEmitter *emitter, YamlOutput *parent)
	: YamlOutput(emitter, parent)
{
}

/**
 * \fn YamlDict &YamlDict::YamlDict(YamlDict &&other)
 * \copydoc YamlOutput &YamlOutput::YamlOutput(YamlOutput &&other)
 */

YamlDict::~YamlDict()
{
	emitMappingEnd();
}

/**
 * \fn YamlDict &YamlDict::operator=(YamlDict &&other)
 * \copydoc YamlOutput &operator=(YamlOutput &&other)
 */

/**
 * \brief Create a list associated with \a key
 * \param[in] key The key to associate the list with
 * \return An instance of YamlList
 */
YamlList YamlDict::list(std::string_view key)
{
	if (!parent_) {
		LOG(YamlEmitter, Error)
			<< "Invalid usage of the YamlEmitter API. "
			<< " The YAML output might not be correct.";
		return {};
	}

	int ret = emitScalar(key);
	if (ret)
		return {};

	ret = emitSequenceStart();
	if (ret)
		return {};

	return YamlOutput::list(this);
}

/**
 * \brief Create a dictionary associated with \a key
 * \param[in] key The key to associate the dictionary with
 * \return An instance of YamlDict
 */
YamlDict YamlDict::dict(std::string_view key)
{
	if (!parent_) {
		LOG(YamlEmitter, Error)
			<< "Invalid usage of the YamlEmitter API. "
			<< " The YAML output might not be correct.";
		return {};
	}

	int ret = emitScalar(key);
	if (ret)
		return {};

	ret = emitMappingStart();
	if (ret)
		return {};

	return YamlOutput::dict(this);
}

/**
 * \brief Emit \a scalar associated with \a key in the dictionary
 * \param[in] key The key associated with the newly created scalar
 * \param[in] scalar The scalar to emit
 */
void YamlDict::scalar(std::string_view key, std::string_view scalar)
{
	if (!parent_) {
		LOG(YamlEmitter, Error)
			<< "Invalid usage of the YamlEmitter API. "
			<< " The YAML output might not be correct.";
		return;
	}

	int ret = emitScalar(key);
	if (ret)
		return;

	emitScalar(scalar);
}

} /* namespace libcamera */
