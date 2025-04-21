#pragma once

#include <assert.h>

#include "sdl_texture.h"

class SDLTexture1Plane final : public SDLTexture
{
public:
	using SDLTexture::SDLTexture;

	void update(libcamera::Span<const libcamera::Span<const uint8_t>> data) override
	{
		assert(data.size() == 1);
		assert(data[0].size_bytes() == std::size_t(rect_.h * stride_));
		SDL_UpdateTexture(ptr_, nullptr, data[0].data(), stride_);
	}
};
