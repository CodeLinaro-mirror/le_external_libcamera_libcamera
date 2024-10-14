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
 * The YAML Emitter helpers allows users to emit output in YAML format.
 *
 * To emit YAML users of this classes should create a root node with
 *
 * \code
	std::string filePath("...");
	auto root = YamlEmitter::root(filePath);
   \endcode
 *
 * A YamlRoot implements RAII-style handling of YAML sequence and document
 * events handling. Creating a YamlRoot emits the STREAM_START and DOC_START
 * events. Once a YamlRoot gets deleted the DOC_SEND and STREAM_END events
 * gets automatically deleted.
 *
 * Once a root node has been created it is possible to populate it with
 * scalars, list or dictionaries.
 *
 * YamlList and YamlDict can only be created wrapped in unique_ptr<> instances,
 * to implement a RAII-style handling of YAML of lists and dictionaries.
 * Creating a YamlList and a YamlDict emits the YAML sequence and mapping start
 * events. Once an instance gets deleted, the sequence and mapping gets
 * automatically emitted. If a list of dictionary needs to be closed before
 * it gets deleted, it can be explicitly closed with the close() function.
 *
 * A YamlScalar is a simpler object that can be assigned to different types
 * to emit them as strings.
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
		ret = errno;
		LOG(YamlEmitter, Error) << "Write error : " << strerror(ret);
		return 0;
	}

	return 1;
}

} /* namespace */

/**
 * \class YamlEvent
 *
 * Class that represents a yaml_event that automatically cleans-up on
 * destruction.
 */
class YamlEvent
{
public:
	static std::unique_ptr<YamlEvent> create();

	const yaml_event_t *event() const
	{
		return &event_;
	}

	yaml_event_t *event()
	{
		return &event_;
	}

private:
	YamlEvent() = default;

	yaml_event_t event_;
};

std::unique_ptr<YamlEvent> YamlEvent::create()
{
	struct Deleter : std::default_delete<YamlEvent> {
		void operator()(YamlEvent *yamlEvent)
		{
			yaml_event_delete(yamlEvent->event());
		}
	};

	return std::unique_ptr<YamlEvent>(new YamlEvent(), Deleter());
}

/**
 * \class YamlEmitter
 *
 * Yaml Emitter entry point. Allows to create a YamlRoot object that users
 * can populate.
 */

/**
 * \brief Create a YamlRoot that applications can start populating with YamlOutput
 * \param[in] path The YAML output file path
 * \return A unique pointer to a YamlRoot
 */
std::unique_ptr<YamlRoot> YamlEmitter::root(std::string_view path)
{
	YamlEmitter *emitter = new YamlEmitter();

	std::string filePath(path);
	emitter->file_ = std::make_unique<File>(filePath);
	emitter->file_->open(File::OpenModeFlag::WriteOnly);

	emitter->init();

	YamlRoot *root = new YamlRoot(emitter);
	return std::unique_ptr<YamlRoot>(root);
}

/**
 * \brief Emit a YamlEvent
 */
int YamlEmitter::emit(YamlEvent *event)
{
	return emitter_.emit(event);
}

void YamlEmitter::init()
{
	emitter_.init(file_.get());

	std::unique_ptr<YamlEvent> event = YamlEvent::create();

	yaml_stream_start_event_initialize(event->event(), YAML_UTF8_ENCODING);
	emitter_.emit(event.get());

	yaml_document_start_event_initialize(event->event(), NULL, NULL,
					     NULL, 0);
	emitter_.emit(event.get());
}

/**
 * \class YamlEmitter::Emitter
 *
 * yaml_emitter_t wrapper. Initialize the yaml_emitter_t on creation allows
 * YamlOutput classes to emit events.
 */

YamlEmitter::Emitter::~Emitter()
{
	yaml_emitter_delete(&emitter_);
}

void YamlEmitter::Emitter::init(File *file)
{
	yaml_emitter_initialize(&emitter_);
	yaml_emitter_set_output(&emitter_, yamlWrite, file);
}

void YamlEmitter::Emitter::logError()
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

int YamlEmitter::Emitter::emit(YamlEvent *event)
{
	int ret = yaml_emitter_emit(&emitter_, event->event());
	if (!ret) {
		logError();
		return -EINVAL;
	}

	return 0;
}

/**
 * \class YamlOutput
 *
 * The YamlOutput base class. From this class YamlScalar, YamlList and YamlDict
 * are derived.
 *
 * The YamlOutput class provides functions to create a scalar, a list or a
 * dictionary.
 *
 * The class cannot be instantiated directly by applications.
 */

std::unique_ptr<YamlScalar> YamlOutput::scalar()
{
	return std::unique_ptr<YamlScalar>(new YamlScalar(emitter_));
}

std::unique_ptr<YamlDict> YamlOutput::dict()
{
	return std::unique_ptr<YamlDict>(new YamlDict(emitter_));
}

std::unique_ptr<YamlList> YamlOutput::list()
{
	return std::unique_ptr<YamlList>(new YamlList(emitter_));
}

int YamlOutput::emitScalar(std::string_view scalar)
{
	std::unique_ptr<YamlEvent> event = YamlEvent::create();

	const unsigned char *value = reinterpret_cast<const unsigned char *>
				     (scalar.data());
	yaml_scalar_event_initialize(event->event(), NULL, NULL, value,
				     scalar.length(), true, false,
				     YAML_PLAIN_SCALAR_STYLE);
	return emitter_->emit(event.get());
}

int YamlOutput::emitMappingStart()
{
	std::unique_ptr<YamlEvent> event = YamlEvent::create();
	yaml_mapping_start_event_initialize(event->event(), NULL, NULL,
					    true, YAML_BLOCK_MAPPING_STYLE);
	return emitter_->emit(event.get());
}

int YamlOutput::emitMappingEnd()
{
	std::unique_ptr<YamlEvent> event = YamlEvent::create();
	yaml_mapping_end_event_initialize(event->event());
	return emitter_->emit(event.get());
}

int YamlOutput::emitSequenceStart()
{
	std::unique_ptr<YamlEvent> event = YamlEvent::create();
	yaml_sequence_start_event_initialize(event->event(), NULL, NULL, true,
					     YAML_BLOCK_SEQUENCE_STYLE);
	return emitter_->emit(event.get());
}

int YamlOutput::emitSequenceEnd()
{
	std::unique_ptr<YamlEvent> event = YamlEvent::create();
	yaml_sequence_end_event_initialize(event->event());
	return emitter_->emit(event.get());
}

/**
 * \class YamlRoot
 *
 * Yaml root node. A root node can be populated with a scalar, a list or a dict.
 */

YamlRoot::~YamlRoot()
{
	std::unique_ptr<YamlEvent> event = YamlEvent::create();

	yaml_document_end_event_initialize(event->event(), 0);
	emitterRoot_->emit(event.get());

	yaml_stream_end_event_initialize(event->event());
	emitterRoot_->emit(event.get());
}

std::unique_ptr<YamlDict> YamlRoot::dict()
{
	emitMappingStart();

	return YamlOutput::dict();
}

std::unique_ptr<YamlList> YamlRoot::list()
{
	emitSequenceStart();

	return YamlOutput::list();
}

/**
 * \class YamlScalar
 *
 * A YamlScalar can be assigned to an std::string_view and other libcamera
 * types to emit them as YAML scalars.
 */

YamlScalar::YamlScalar(YamlEmitter *emitter)
	: YamlOutput(emitter)
{
}

void YamlScalar::operator=(const libcamera::Orientation &orientation)
{
	std::stringstream o;
	o << orientation;

	emitScalar(o.str());
}

void YamlScalar::operator=(std::string_view scalar)
{
	emitScalar(scalar);
}

/**
 * \class YamlList
 *
 * A YamlList can be populated with scalar and allows to create other lists
 * and dictionaries.
 */

YamlList::YamlList(YamlEmitter *emitter)
	: YamlOutput(emitter)
{
}

YamlList::~YamlList()
{
	if (!closed_)
		close();
}

void YamlList::close()
{
	emitSequenceEnd();
	YamlOutput::close();
}

void YamlList::scalar(std::string_view scalar)
{
	emitScalar(scalar);
}

std::unique_ptr<YamlList> YamlList::list()
{
	emitSequenceStart();

	return YamlOutput::list();
}

std::unique_ptr<YamlDict> YamlList::dict()
{
	emitMappingStart();

	return YamlOutput::dict();
}

/**
 * \class YamlDict
 *
 * A YamlDict derives an unordered_map that maps strings to YAML scalar.
 *
 * A YamlDict can be populated with scalars using operator[] and allows to
 * create other lists and dictionaries.
 */

YamlDict::YamlDict(YamlEmitter *emitter)
	: YamlOutput(emitter)
{
}

YamlDict::~YamlDict()
{
	if (!closed_)
		close();

	clear();
}

void YamlDict::close()
{
	emitMappingEnd();
	YamlOutput::close();
}

std::unique_ptr<YamlList> YamlDict::list(std::string_view key)
{
	emitScalar(key);
	emitSequenceStart();

	return YamlOutput::list();
}

std::unique_ptr<YamlDict> YamlDict::dict(std::string_view key)
{
	emitScalar(key);
	emitMappingStart();

	return YamlOutput::dict();
}

YamlScalar &YamlDict::operator[](const YamlDict::Map::key_type &key)
{
	emplace(key, YamlOutput::scalar());
	emitScalar(key);

	return *at(key);
}

} /* namespace libcamera */
