/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (C) 2026, Alain Cousinié
 * raw_mono_unpacked.frag - Fragment shader for raw unpacked monochrome formats (R8, R10, R12)
 */

#extension GL_EXT_texture_rg : enable

#ifdef GL_ES
precision highp float;
#endif

varying vec4            center;
varying vec4            xCoord;
varying vec4            yCoord;

uniform sampler2D 	tex_y;
uniform float 		gamma;
uniform float 		contrastExp;

uniform vec3 		blacklevel;
uniform vec3 		awb;

float applyContrast(float value)
{
	float isAbove = step(0.5, value);
	float lowCase = 0.5 * pow(value * 2.0, contrastExp);
	float highCase = 1.0 - 0.5 * pow((1.0 - value) * 2.0, contrastExp);
	return mix(lowCase, highCase, isAbove);
}

void main(void)
{
	vec4 texel = texture2D(tex_y, center.xy);

	float grayBits = 0.0;
	float maxVal = 1.0;

#ifdef MONO_RAW8
	grayBits = floor(texel.r * 255.0 + 0.5);
	maxVal = 255.0;
#endif

#ifdef MONO_RAW10
	float byteLow10  = floor(texel.r * 255.0 + 0.5);
	float byteHigh10 = floor(texel.g * 255.0 + 0.5);
	grayBits = (byteHigh10 * 256.0) + byteLow10;
	maxVal = 1023.0;
#endif

#ifdef MONO_RAW12
	float byteLow12  = floor(texel.r * 255.0 + 0.5);
	float byteHigh12 = floor(texel.g * 255.0 + 0.5);
	float nativeVal12 = (byteHigh12 * 256.0) + byteLow12;
	grayBits = floor(nativeVal12 * 0.0625);
	maxVal = 4095.0;
#endif

	float cVal = grayBits / maxVal;

	cVal = (cVal - blacklevel.g) * awb.g;
	cVal = clamp(cVal, 0.0, 1.0);

	cVal = applyContrast(cVal);

	vec3 rgb = vec3(cVal);
	rgb = pow(rgb, vec3(gamma));

	gl_FragColor = vec4(rgb, 1.0);
}
