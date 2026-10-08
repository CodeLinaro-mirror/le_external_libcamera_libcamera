/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (C) 2026, Alain Cousinié
 * raw_mono_1x_packed.frag - Fragment shader for raw packed monochrome formats (R10P, R12P)
 */


#ifdef GL_ES
precision highp float;
#endif

varying vec2            textureOut;

uniform sampler2D       tex_y;
uniform vec2            tex_size;
uniform float           gamma;
uniform float           contrastExp;

uniform vec3 		blacklevel;
uniform vec3 		awb;

float applyContrast(float value)
{
	float isAbove = step(0.5, value);
	float lowCase = 0.5 * pow(value * 2.0, contrastExp);
	float highCase = 1.0 - 0.5 * pow((1.0 - value) * 2.0, contrastExp);
	return mix(lowCase, highCase, isAbove);
}

float readByte(float pixelX, float pixelY)
{
	vec2 uv = vec2(pixelX + 0.5, pixelY + 0.5) / tex_size;
	return floor(texture2D(tex_y, uv).r * 255.0 + 0.5);
}

void main(void)
{
	vec2 pixelCoords = floor(textureOut * tex_size);
	float px = pixelCoords.x;
	float py = pixelCoords.y;

	float grayBits = 0.0;
	float maxVal = 1.0;

#ifdef MONO_RAW10
	int index10 = int(mod(px, 4.0));
	float blockStart10 = floor(px / 4.0) * 5.0;

	float g10 = readByte(blockStart10 + float(index10), py);
	float lsb10 = readByte(blockStart10 + 4.0, py);

	float divisor10 = exp2(2.0 * float(index10));
	float lsbBits10 = mod(floor(lsb10 / divisor10), 4.0);

	grayBits = (g10 * 4.0) + lsbBits10;
	maxVal = 1023.0;
#endif

#ifdef MONO_RAW12
	int index12 = int(mod(px, 2.0));
	float blockStart12 = floor(px / 2.0) * 3.0;

	float g12 = readByte(blockStart12 + float(index12), py);
	float lsb12 = readByte(blockStart12 + 2.0, py);

	float divisor12 = (index12 == 0) ? 1.0 : 16.0;
	float lsbBits12 = mod(floor(lsb12 / divisor12), 16.0);

	grayBits = (g12 * 16.0) + lsbBits12;
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
