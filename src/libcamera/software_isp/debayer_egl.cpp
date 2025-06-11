/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2024, Linaro Ltd.
 *
 * Authors:
 * Bryan O'Donoghue <bryan.odonoghue@linaro.org>
 *
 * debayer_cpu.cpp - EGL based debayering class
 */

#include <math.h>
#include <stdlib.h>
#include <time.h>

#include <libcamera/formats.h>

#include "libcamera/internal/glsl_shaders.h"
#include "debayer_egl.h"

namespace libcamera {

DebayerEGL::DebayerEGL(std::unique_ptr<SwStatsCpu> stats)
	: Debayer(), stats_(std::move(stats))
{
	eglImageBayerIn_ = eglImageRedLookup_ = eglImageBlueLookup_ = eglImageGreenLookup_ = NULL;
}

DebayerEGL::~DebayerEGL()
{
	if (eglImageBlueLookup_)
		delete eglImageBlueLookup_;

	if (eglImageGreenLookup_)
		delete eglImageGreenLookup_;

	if (eglImageRedLookup_)
		delete eglImageRedLookup_;

	if (eglImageBayerIn_)
		delete eglImageBayerIn_;
}

int DebayerEGL::getInputConfig(PixelFormat inputFormat, DebayerInputConfig &config)
{
	BayerFormat bayerFormat =
		BayerFormat::fromPixelFormat(inputFormat);

	if ((bayerFormat.bitDepth == 8 || bayerFormat.bitDepth == 10) &&
	    bayerFormat.packing == BayerFormat::Packing::None &&
	    isStandardBayerOrder(bayerFormat.order)) {
		config.bpp = (bayerFormat.bitDepth + 7) & ~7;
		config.patternSize.width = 2;
		config.patternSize.height = 2;
		config.outputFormats = std::vector<PixelFormat>({ formats::XRGB8888,
								  formats::ARGB8888,
								  formats::XBGR8888,
								  formats::ABGR8888 });
		return 0;
	}

	if (bayerFormat.bitDepth == 10 &&
	    bayerFormat.packing == BayerFormat::Packing::CSI2 &&
	    isStandardBayerOrder(bayerFormat.order)) {
		config.bpp = 10;
		config.patternSize.width = 4; /* 5 bytes per *4* pixels */
		config.patternSize.height = 2;
		config.outputFormats = std::vector<PixelFormat>({ formats::XRGB8888,
								  formats::ARGB8888,
								  formats::XBGR8888,
								  formats::ABGR8888 });
		return 0;
	}

	LOG(Debayer, Info)
		<< "Unsupported input format " << inputFormat.toString();
	return -EINVAL;
}

int DebayerEGL::getOutputConfig(PixelFormat outputFormat, DebayerOutputConfig &config)
{
	if (outputFormat == formats::XRGB8888 || outputFormat == formats::ARGB8888 ||
	    outputFormat == formats::XBGR8888 || outputFormat == formats::ABGR8888) {
		config.bpp = 32;
		return 0;
	}

	LOG(Debayer, Error)
		<< "Unsupported output format " << outputFormat.toString();

	return -EINVAL;
}

int DebayerEGL::getShaderVariableLocations(void)
{
	attributeVertex_ = glGetAttribLocation(programId_, "vertexIn");
	attributeTexture_ = glGetAttribLocation(programId_, "textureIn");

	textureUniformBayerDataIn_ = glGetUniformLocation(programId_, "tex_y");
	textureUniformRedLookupDataIn_ = glGetUniformLocation(programId_, "red_param");
	textureUniformGreenLookupDataIn_ = glGetUniformLocation(programId_, "green_param");
	textureUniformBlueLookupDataIn_ = glGetUniformLocation(programId_, "blue_param");

	textureUniformStep_ = glGetUniformLocation(programId_, "tex_step");
	textureUniformSize_ = glGetUniformLocation(programId_, "tex_size");
	textureUniformStrideFactor_ = glGetUniformLocation(programId_, "stride_factor");
	textureUniformBayerFirstRed_ = glGetUniformLocation(programId_, "tex_bayer_first_red");
	textureUniformProjMatrix_ = glGetUniformLocation(programId_, "proj_matrix");

	LOG(Debayer, Debug) << "vertexIn " << attributeVertex_ << " textureIn " << attributeTexture_
			    << " tex_y " << textureUniformBayerDataIn_
			    << " red_param " << textureUniformRedLookupDataIn_
			    << " green_param " << textureUniformGreenLookupDataIn_
			    << " blue_param " << textureUniformBlueLookupDataIn_
			    << " tex_step " << textureUniformStep_
			    << " tex_size " << textureUniformSize_
			    << " stride_factor " << textureUniformStrideFactor_
			    << " tex_bayer_first_red " << textureUniformBayerFirstRed_
			    << " proj_matrix " << textureUniformProjMatrix_;
	return 0;
}

int DebayerEGL::initBayerShaders(PixelFormat inputFormat, PixelFormat outputFormat)
{
	std::vector<std::string> shaderEnv;
	unsigned int fragmentShaderDataLen;
	unsigned char *fragmentShaderData;
	unsigned int vertexShaderDataLen;
	unsigned char *vertexShaderData;
	GLenum err;

	// Target gles 100 glsl requires "#version x" as first directive in shader
	egl_.pushEnv(shaderEnv, "#version 100");

	// Specify GL_OES_EGL_image_external
	egl_.pushEnv(shaderEnv, "#extension GL_OES_EGL_image_external: enable");

	// Tell shaders how to re-order output taking account of how the
	// pixels are actually stored by GBM
	switch (outputFormat) {
	case formats::ARGB8888:
	case formats::XRGB8888:
		break;
	case formats::ABGR8888:
	case formats::XBGR8888:
		egl_.pushEnv(shaderEnv, "#define SWAP_BLUE");
		break;
	default:
		goto invalid_fmt;
	}

	// Pixel location parameters
	switch (inputFormat) {
	case libcamera::formats::SBGGR8:
	case libcamera::formats::SBGGR10_CSI2P:
	case libcamera::formats::SBGGR12_CSI2P:
		firstRed_x_ = 1.0;
		firstRed_y_ = 1.0;
		break;
	case libcamera::formats::SGBRG8:
	case libcamera::formats::SGBRG10_CSI2P:
	case libcamera::formats::SGBRG12_CSI2P:
		firstRed_x_ = 0.0;
		firstRed_y_ = 1.0;
		break;
	case libcamera::formats::SGRBG8:
	case libcamera::formats::SGRBG10_CSI2P:
	case libcamera::formats::SGRBG12_CSI2P:
		firstRed_x_ = 1.0;
		firstRed_y_ = 0.0;
		break;
	case libcamera::formats::SRGGB8:
	case libcamera::formats::SRGGB10_CSI2P:
	case libcamera::formats::SRGGB12_CSI2P:
		firstRed_x_ = 0.0;
		firstRed_y_ = 0.0;
		break;
	default:
		goto invalid_fmt;
		break;
	};

	// Shader selection
	switch (inputFormat) {
	case libcamera::formats::SBGGR8:
	case libcamera::formats::SGBRG8:
	case libcamera::formats::SGRBG8:
	case libcamera::formats::SRGGB8:
		fragmentShaderData = bayer_8_frag;
		fragmentShaderDataLen = bayer_8_frag_len;
		vertexShaderData = bayer_8_vert;
		vertexShaderDataLen = bayer_8_vert_len;
		break;
	case libcamera::formats::SBGGR10_CSI2P:
	case libcamera::formats::SGBRG10_CSI2P:
	case libcamera::formats::SGRBG10_CSI2P:
	case libcamera::formats::SRGGB10_CSI2P:
		egl_.pushEnv(shaderEnv, "#define RAW10P");
		fragmentShaderData = bayer_1x_packed_frag;
		fragmentShaderDataLen = bayer_1x_packed_frag_len;
		vertexShaderData = identity_vert;
		vertexShaderDataLen = identity_vert_len;
		break;
	case libcamera::formats::SBGGR12_CSI2P:
	case libcamera::formats::SGBRG12_CSI2P:
	case libcamera::formats::SGRBG12_CSI2P:
	case libcamera::formats::SRGGB12_CSI2P:
		egl_.pushEnv(shaderEnv, "#define RAW12P");
		fragmentShaderData = bayer_1x_packed_frag;
		fragmentShaderDataLen = bayer_1x_packed_frag_len;
		vertexShaderData = identity_vert;
		vertexShaderDataLen = identity_vert_len;
		break;
	default:
		goto invalid_fmt;
		break;
	};

	// Flag to shaders that we have parameter gain tables
	egl_.pushEnv(shaderEnv, "#define APPLY_RGB_PARAMETERS");

	if (egl_.compileVertexShader(vertexShaderId_, vertexShaderData, vertexShaderDataLen, shaderEnv))
		goto compile_fail;

	if (egl_.compileFragmentShader(fragmentShaderId_, fragmentShaderData, fragmentShaderDataLen, shaderEnv))
		goto compile_fail;

	if (egl_.linkProgram(programId_, vertexShaderId_, fragmentShaderId_))
		goto link_fail;

	egl_.dumpShaderSource(vertexShaderId_);
	egl_.dumpShaderSource(fragmentShaderId_);

	/* Ensure we set the programId_ */
	egl_.useProgram(programId_);
	err = glGetError();
	if (err != GL_NO_ERROR)
		goto program_fail;

	if (getShaderVariableLocations())
		goto parameters_fail;

	return 0;

parameters_fail:
	LOG(Debayer, Error) << "Program parameters fail";
	return -ENODEV;

program_fail:
	LOG(Debayer, Error) << "Use program error " << err;
	return -ENODEV;

link_fail:
	LOG(Debayer, Error) << "Linking program fail";
	return -ENODEV;

compile_fail:
	LOG(Debayer, Error) << "Compile debayer shaders fail";
	return -ENODEV;

invalid_fmt:
	LOG(Debayer, Error) << "Unsupported input output format combination";
	return -EINVAL;
}

int DebayerEGL::configure(const StreamConfiguration &inputCfg,
			  const std::vector<std::reference_wrapper<StreamConfiguration>> &outputCfgs,
			  bool ccmEnabled)
{
	if (getInputConfig(inputCfg.pixelFormat, inputConfig_) != 0)
		return -EINVAL;

	if (stats_->configure(inputCfg) != 0)
		return -EINVAL;

	const Size &stats_pattern_size = stats_->patternSize();
	if (inputConfig_.patternSize.width != stats_pattern_size.width ||
	    inputConfig_.patternSize.height != stats_pattern_size.height) {
		LOG(Debayer, Error)
			<< "mismatching stats and debayer pattern sizes for "
			<< inputCfg.pixelFormat.toString();
		return -EINVAL;
	}

	inputConfig_.stride = inputCfg.stride;
	width_ = inputCfg.size.width;
	height_ = inputCfg.size.height;
	ccmEnabled_ = ccmEnabled = false;

	if (outputCfgs.size() != 1) {
		LOG(Debayer, Error)
			<< "Unsupported number of output streams: "
			<< outputCfgs.size();
		return -EINVAL;
	}

	LOG(Debayer, Info) << "Input size " << inputCfg.size << " stride " << inputCfg.stride;

	if (gbmSurface_.initSurface(inputCfg.size.width, inputCfg.size.height))
		return -ENODEV;

	if (egl_.initEGLContext(&gbmSurface_))
		return -ENODEV;

	glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &maxTextureImageUnits);
	LOG(Debayer, Debug) << "Fragment shader maximum texture units " << maxTextureImageUnits;

	if (maxTextureImageUnits < DEBAYER_EGL_MIN_SIMPLE_RGB_GAIN_TEXTURE_UNITS) {
		LOG(Debayer, Error) << "Fragment shader texture unit count " << maxTextureImageUnits
				    << " required minimum for RGB gain table lookup " << DEBAYER_EGL_MIN_SIMPLE_RGB_GAIN_TEXTURE_UNITS
				    << " try using an identity CCM ";
		return -ENODEV;
	}
	// Raw bayer input as texture
	eglImageBayerIn_ = new eGLImage(width_, height_, 32, GL_TEXTURE0, 0);
	if (!eglImageBayerIn_)
		return -ENOMEM;

	/// RGB correction tables as 2d textures
	// eGL doesn't support glTexImage1D so we do a little hack with 2D to compensate
	eglImageRedLookup_ = new eGLImage(DebayerParams::kRGBLookupSize, 1, 32, GL_TEXTURE1, 1);
	if (!eglImageRedLookup_)
		return -ENOMEM;

	eglImageGreenLookup_ = new eGLImage(DebayerParams::kRGBLookupSize, 1, 32, GL_TEXTURE2, 2);
	if (!eglImageGreenLookup_)
		return -ENOMEM;

	eglImageBlueLookup_ = new eGLImage(DebayerParams::kRGBLookupSize, 1, 32, GL_TEXTURE3, 3);
	if (!eglImageBlueLookup_)
		return -ENOMEM;

	// Create a single BO (calling gbm_surface_lock_front_buffer() again before gbm_surface_release_buffer() would create another BO)
	if (gbmSurface_.mapSurface())
		return -ENODEV;

	StreamConfiguration &outputCfg = outputCfgs[0];
	SizeRange outSizeRange = sizes(inputCfg.pixelFormat, inputCfg.size);

	outputConfig_.stride = gbmSurface_.getStride();
	outputConfig_.frameSize = gbmSurface_.getFrameSize();

	LOG(Debayer, Debug) << "Overriding stream config stride "
			    << outputCfg.stride << " with GBM surface stride "
			    << outputConfig_.stride;
	outputCfg.stride = outputConfig_.stride;

	if (!outSizeRange.contains(outputCfg.size) || outputConfig_.stride != outputCfg.stride) {
		LOG(Debayer, Error)
			<< "Invalid output size/stride: "
			<< "\n  " << outputCfg.size << " (" << outSizeRange << ")"
			<< "\n  " << outputCfg.stride << " (" << outputConfig_.stride << ")";
		return -EINVAL;
	}

	window_.x = ((inputCfg.size.width - outputCfg.size.width) / 2) &
		    ~(inputConfig_.patternSize.width - 1);
	window_.y = ((inputCfg.size.height - outputCfg.size.height) / 2) &
		    ~(inputConfig_.patternSize.height - 1);
	window_.width = outputCfg.size.width;
	window_.height = outputCfg.size.height;

	/* Don't pass x,y since process() already adjusts src before passing it */
	stats_->setWindow(Rectangle(window_.size()));

	LOG(Debayer, Debug) << "Input width " << inputCfg.size.width << " height " << inputCfg.size.height;
	LOG(Debayer, Debug) << "Output width " << outputCfg.size.width << " height " << outputCfg.size.height;
	LOG(Debayer, Debug) << "Output stride " << outputCfg.size.width << " height " << outputCfg.size.height;

	if (initBayerShaders(inputCfg.pixelFormat, outputCfg.pixelFormat))
		return -EINVAL;

	return 0;
}

Size DebayerEGL::patternSize(PixelFormat inputFormat)
{
	DebayerEGL::DebayerInputConfig config;

	if (getInputConfig(inputFormat, config) != 0)
		return {};

	return config.patternSize;
}

std::vector<PixelFormat> DebayerEGL::formats(PixelFormat inputFormat)
{
	DebayerEGL::DebayerInputConfig config;

	if (getInputConfig(inputFormat, config) != 0)
		return std::vector<PixelFormat>();

	return config.outputFormats;
}

std::tuple<unsigned int, unsigned int>
DebayerEGL::strideAndFrameSize(const PixelFormat &outputFormat, const Size &size)
{
	DebayerEGL::DebayerOutputConfig config;

	if (getOutputConfig(outputFormat, config) != 0)
		return std::make_tuple(0, 0);

	/* round up to multiple of 8 for 64 bits alignment */
	unsigned int stride = (size.width * config.bpp / 8 + 7) & ~7;

	return std::make_tuple(stride, stride * size.height);
}

void DebayerEGL::setShaderVariableValues(void)
{
	/*
	 * Raw Bayer 8-bit, and packed raw Bayer 10-bit/12-bit formats
	 * are stored in a GL_LUMINANCE texture. The texture width is
	 * equal to the stride.
	 */
	GLfloat firstRed[] = { firstRed_x_, firstRed_y_ };
	GLfloat imgSize[] = { (GLfloat)width_,
			      (GLfloat)height_ };
	GLfloat Step[] = { 1.0f / (inputConfig_.stride - 1),
			   1.0f / (height_ - 1) };
	GLfloat Stride = 1.0f;
	GLfloat projIdentityMatrix[] = {
		1, 0, 0, 0,
		0, 1, 0, 0,
		0, 0, 1, 0,
		0, 0, 0, 1
	};

	// vertexIn - bayer_8.vert
	glEnableVertexAttribArray(attributeVertex_);
	glVertexAttribPointer(attributeVertex_, 2, GL_FLOAT, GL_TRUE,
			      2 * sizeof(GLfloat), vcoordinates);

	// textureIn - bayer_8.vert
	glEnableVertexAttribArray(attributeTexture_);
	glVertexAttribPointer(attributeTexture_, 2, GL_FLOAT, GL_TRUE,
			      2 * sizeof(GLfloat), tcoordinates);

	// Set the sampler2D to the respective texture unit for each texutre
	// To simultaneously sample multiple textures we need to use multiple
	// texture units
	glUniform1i(textureUniformBayerDataIn_, eglImageBayerIn_->texture_unit_uniform_id_);
	glUniform1i(textureUniformRedLookupDataIn_, eglImageRedLookup_->texture_unit_uniform_id_);
	glUniform1i(textureUniformGreenLookupDataIn_, eglImageGreenLookup_->texture_unit_uniform_id_);
	glUniform1i(textureUniformBlueLookupDataIn_, eglImageBlueLookup_->texture_unit_uniform_id_);

	// These values are:
	// firstRed = tex_bayer_first_red - bayer_8.vert
	// imgSize = tex_size - bayer_8.vert
	// step = tex_step - bayer_8.vert
	// Stride = stride_factor identity.vert
	// textureUniformProjMatri = No scaling
	glUniform2fv(textureUniformBayerFirstRed_, 1, firstRed);
	glUniform2fv(textureUniformSize_, 1, imgSize);
	glUniform2fv(textureUniformStep_, 1, Step);
	glUniform1f(textureUniformStrideFactor_, Stride);
	glUniformMatrix4fv(textureUniformProjMatrix_, 1,
			   GL_FALSE, projIdentityMatrix);

	LOG(Debayer, Debug) << "vertexIn " << attributeVertex_ << " textureIn " << attributeTexture_
			    << " tex_y " << textureUniformBayerDataIn_
			    << " red_param " << textureUniformRedLookupDataIn_
			    << " green_param " << textureUniformGreenLookupDataIn_
			    << " blue_param " << textureUniformBlueLookupDataIn_
			    << " tex_step " << textureUniformStep_
			    << " tex_size " << textureUniformSize_
			    << " stride_factor " << textureUniformStrideFactor_
			    << " tex_bayer_first_red " << textureUniformBayerFirstRed_;

	LOG (Debayer, Debug) << "textureUniformY_ = 0 " <<
				" firstRed.x " << firstRed[0] <<
				" firstRed.y " << firstRed[1] <<
				" textureUniformSize_.width " << imgSize[0] << " "
				" textureUniformSize_.height " << imgSize[1] <<
				" textureUniformStep_.x " << Step[0] <<
				" textureUniformStep_.y " << Step[1] <<
				" textureUniformStrideFactor_ " << Stride <<
				" textureUniformProjMatrix_ " << textureUniformProjMatrix_;
	return;
}

void DebayerEGL::debayerGPU(MappedFrameBuffer &in, MappedFrameBuffer &out, DebayerParams &params)
{
	LOG(Debayer, Debug)
		<< "Input height " << height_
		<< " width " << width_
		<< " fd " << in.getPlaneFD(0);

	// eGL context switch
	egl_.makeCurrent();

	// Greate a standard texture
	// we will replace this with the DMA version at some point
	egl_.createTexture2D(eglImageBayerIn_, inputConfig_.stride, height_, in.planes()[0].data());

	// Populate bayer parameters
	egl_.createTexture2D(eglImageRedLookup_, DebayerParams::kRGBLookupSize, 1, &params.red);
	egl_.createTexture2D(eglImageGreenLookup_, DebayerParams::kRGBLookupSize, 1, &params.green);
	egl_.createTexture2D(eglImageBlueLookup_, DebayerParams::kRGBLookupSize, 1, &params.blue);

	// Setup the scene
	setShaderVariableValues();
	glViewport(0, 0, width_, height_);
	glClear(GL_COLOR_BUFFER_BIT);
	glDisable(GL_BLEND);

	// Draw the scene
	glDrawArrays(GL_TRIANGLE_FAN, 0, DEBAYER_OPENGL_COORDS);

	// eglclientWaitScynKhr / eglwaitsynckr ?
	egl_.swapBuffers();

	// Copy from the output GBM buffer to our output plane
	// once we get render to texture working the
	// explicit lock ioctl, memcpy and unlock ioctl won't be required
	gbmSurface_.getFrameBufferData(out.planes()[0].data(), out.planes()[0].size());
}

void DebayerEGL::process(uint32_t frame, FrameBuffer *input, FrameBuffer *output, DebayerParams params)
{
	bench_.startFrame();

	std::vector<DmaSyncer> dmaSyncers;

	dmaSyncBegin(dmaSyncers, input, output);

	setParams(params);

	/* Copy metadata from the input buffer */
	FrameMetadata &metadata = output->_d()->metadata();
	metadata.status = input->metadata().status;
	metadata.sequence = input->metadata().sequence;
	metadata.timestamp = input->metadata().timestamp;

	MappedFrameBuffer in(input, MappedFrameBuffer::MapFlag::Read);
	MappedFrameBuffer out(output, MappedFrameBuffer::MapFlag::Write);
	if (!in.isValid() || !out.isValid()) {
		LOG(Debayer, Error) << "mmap-ing buffer(s) failed";
		metadata.status = FrameMetadata::FrameError;
		return;
	}

	debayerGPU(in, out, params);

	dmaSyncers.clear();

	bench_.finishFrame();

	metadata.planes()[0].bytesused = out.planes()[0].size();

	// Calculate stats for the whole frame
	stats_->processFrame(frame, 0, input);

	outputBufferReady.emit(output);
	inputBufferReady.emit(input);
}

SizeRange DebayerEGL::sizes(PixelFormat inputFormat, const Size &inputSize)
{
	Size patternSize = this->patternSize(inputFormat);
	unsigned int borderHeight = patternSize.height;

	if (patternSize.isNull())
		return {};

	/* No need for top/bottom border with a pattern height of 2 */
	if (patternSize.height == 2)
		borderHeight = 0;

	/*
	 * For debayer interpolation a border is kept around the entire image
	 * and the minimum output size is pattern-height x pattern-width.
	 */
	if (inputSize.width < (3 * patternSize.width) ||
	    inputSize.height < (2 * borderHeight + patternSize.height)) {
		LOG(Debayer, Warning)
			<< "Input format size too small: " << inputSize.toString();
		return {};
	}

	return SizeRange(Size(patternSize.width, patternSize.height),
			 Size((inputSize.width - 2 * patternSize.width) & ~(patternSize.width - 1),
			      (inputSize.height - 2 * borderHeight) & ~(patternSize.height - 1)),
			 patternSize.width, patternSize.height);
}

} /* namespace libcamera */
