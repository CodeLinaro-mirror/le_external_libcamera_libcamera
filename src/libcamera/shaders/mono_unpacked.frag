/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (C) 2026, Alain Cousinié
 * Unified Fragment shader for unpacked monochrome formats (R8, R10, R12, R16)
 * Optimized for libcamera EGL pipeline (GLSL ES 1.00)
 */

#ifdef GL_ES
#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif
#endif

varying vec4            center;

uniform sampler2D       tex_y;
uniform float           gamma;
uniform float           contrastExp;
uniform vec3            blacklevel;

void main(void)
{
	float rawValue = 0.0;

#ifdef RAW8
	rawValue = floor(texture2D(tex_y, center.xy).r * 255.0 + 0.5);
#else
	vec4 texel = texture2D(tex_y, center.xy);

	float byteLow  = floor(texel.r * 255.0 + 0.5);
	float byteHigh = floor(texel.g * 255.0 + 0.5);

	rawValue = (byteHigh * 256.0) + byteLow;
#endif

	float normalizedIntensity = rawValue * INV_MAX_VAL;
	normalizedIntensity = clamp(normalizedIntensity - blacklevel.g, 0.0, 1.0);

	float centerDist = 1.0 - 2.0 * abs(normalizedIntensity - 0.5);
	float singlePow  = 0.5 * pow(centerDist, contrastExp);
	normalizedIntensity = mix(singlePow, 1.0 - singlePow, step(0.5, normalizedIntensity));

	vec3 rgb = pow(vec3(normalizedIntensity), vec3(gamma));
	gl_FragColor = vec4(rgb, 1.0);
}
