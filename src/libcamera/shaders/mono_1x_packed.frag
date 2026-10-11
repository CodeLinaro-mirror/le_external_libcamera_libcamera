/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (C) 2026, Alain Cousinié
 * Unified Fragment shader for MIPI CSI-2 packed monochrome formats (R10P / R12P)
 * Full bit-depth extraction (with LSB) optimized for HDR & Computer Vision
 */

#ifdef GL_ES
#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif
#endif

varying vec2            textureOut;

uniform vec2            tex_size;
uniform vec2            tex_step;

uniform sampler2D       tex_y;
uniform float           gamma;
uniform float           contrastExp;
uniform vec3            blacklevel;

void main(void)
{
	vec2 pixelCoords = floor(textureOut * tex_size);
	float px = pixelCoords.x;
	float py = pixelCoords.y;

	float blockStart  = floor(px / BLOCK_PIXELS) * BLOCK_BYTES;
	float idx         = px - (blockStart * (BLOCK_PIXELS / BLOCK_BYTES));

	float xMSB        = (blockStart + idx + 0.5) * tex_step.x;
	float xLSB        = (blockStart + BLOCK_PIXELS + 0.5) * tex_step.x;
	float normY       = (py + 0.5) * tex_step.y;

	float byteHigh = floor(texture2D(tex_y, vec2(xMSB, normY)).r * 255.0 + 0.5);
	float byteLow  = floor(texture2D(tex_y, vec2(xLSB, normY)).r * 255.0 + 0.5);

	float lsbMask = 0.0;
	float msbMultiplier = 0.0;
	float shiftedLsb = 0.0;

	vec4 pixelPos = step(vec4(-0.5, 0.5, 1.5, 2.5), vec4(idx)) * step(vec4(idx), vec4(0.5, 1.5, 2.5, 3.5));

#if defined(RAW10P)
	lsbMask = 4.0;
	msbMultiplier = 4.0;
	float divisor = dot(pixelPos, vec4(1.0, 4.0, 16.0, 64.0));
	shiftedLsb = floor(byteLow / divisor);
#elif defined(RAW12P)
	lsbMask = 16.0;
	msbMultiplier = 16.0;
	float divisor = dot(pixelPos, vec4(1.0, 16.0, 0.0, 0.0));
	shiftedLsb = floor(byteLow / divisor);
#endif

	float lsbBits = shiftedLsb - (floor(shiftedLsb / lsbMask) * lsbMask);
	float rawValue = (byteHigh * msbMultiplier) + lsbBits;

	float normalizedIntensity = rawValue * INV_MAX_VAL;
	normalizedIntensity = clamp(normalizedIntensity - blacklevel.g, 0.0, 1.0);

	float centerDist = 1.0 - 2.0 * abs(normalizedIntensity - 0.5);
	float singlePow  = 0.5 * pow(centerDist, contrastExp);
	normalizedIntensity = mix(singlePow, 1.0 - singlePow, step(0.5, normalizedIntensity));

	vec3 rgb = pow(vec3(normalizedIntensity), vec3(gamma));
	gl_FragColor = vec4(rgb, 1.0);
}
