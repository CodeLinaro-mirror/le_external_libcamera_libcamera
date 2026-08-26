/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2026, Robert Bozik
 *
 * temporal.frag - Temporal noise reduction of raw Bayer data
 *
 * Blends the current raw frame with the previously filtered one, before
 * black level subtraction, so that the noise is averaged before any
 * clamping rectifies it. Where the frame changed by more than a few
 * sigmas of the expected sensor noise the current frame is used as is,
 * to avoid ghosting on motion.
 *
 * The filtered frame is kept in an RGBA8 texture, which every GLES 2.0
 * implementation can render to, laid out like the raw input so that the
 * debayer shader can sample either.
 */

#ifdef GL_ES
precision highp float;
#endif

uniform sampler2D tex_y;      /* Current raw frame */
uniform sampler2D tex_hist;   /* Previous filtered frame, packed */
uniform float alpha;          /* Weight of the current frame */
uniform float noise_a;        /* Noise variance per unit of signal */
uniform float noise_b;        /* Noise variance floor */
uniform float motion_k;       /* Motion threshold in noise sigmas */
uniform float black;          /* Black level, normalised */
uniform float hist_valid;     /* 0.0 on the first frame */

varying vec2 textureOut;

/*
 * The history texture is RGBA8 and holds the filtered value in the same
 * layout as the raw input texture, so that the debayer shader samples
 * either one with the same decoding: for the unpacked 10 and 12 bit
 * formats the low byte in .r and the high byte in .g, for 8 bit formats
 * the value in .r. The fraction of the value is kept in .b for the
 * precision of the recursive filter; the debayer shader ignores it.
 *
 * The decoding mirrors bayer_unpacked.frag: (lo + 256 * hi) / 1020 for
 * 10 bit, (lo + 256 * hi) / 4080 for 12 bit.
 */
#if defined(RAW10P)
#define RAW_SCALE 1020.0
#elif defined(RAW12P)
#define RAW_SCALE 4080.0
#endif

#if defined(RAW_SCALE)
float fetch_cur(vec2 uv)
{
	vec4 p = texture2D(tex_y, uv);
	return (p.r * 255.0 + p.g * 255.0 * 256.0) / RAW_SCALE;
}

float fetch_hist(vec2 uv)
{
	vec4 p = texture2D(tex_hist, uv);
	return (p.r * 255.0 + p.g * 255.0 * 256.0 + p.b) / RAW_SCALE;
}

vec4 pack_hist(float v)
{
	float raw = clamp(v, 0.0, 1.0) * RAW_SCALE;
	float ip = floor(raw);
	float hi = floor(ip / 256.0);
	float lo = ip - hi * 256.0;
	return vec4(lo / 255.0, hi / 255.0, raw - ip, 1.0);
}
#else
float fetch_cur(vec2 uv)
{
	return texture2D(tex_y, uv).r;
}

float fetch_hist(vec2 uv)
{
	vec4 p = texture2D(tex_hist, uv);
	return p.r + p.b / 255.0;
}

vec4 pack_hist(float v)
{
	float raw = clamp(v, 0.0, 1.0) * 255.0;
	float ip = floor(raw);
	return vec4(ip / 255.0, 0.0, raw - ip, 1.0);
}
#endif

uniform vec2 tex_step;        /* 1 / texture size, one texel */

void main(void)
{
	float cur = fetch_cur(textureOut);
	float prev = fetch_hist(textureOut);

	/*
	 * Detect motion on the mean of the 4x4 block around the pixel (two
	 * Bayer quads each way) rather than on the pixel alone: the noise of
	 * the mean is a quarter of that of a pixel, so the threshold can sit
	 * close to the real noise and still catch subtle motion, such as the
	 * trailing edge of an object over a background of similar brightness.
	 */
	float curMean = 0.0;
	float prevMean = 0.0;
	for (int j = -1; j <= 2; j++) {
		for (int i = -1; i <= 2; i++) {
			vec2 uv = textureOut + vec2(float(i) * tex_step.x, float(j) * tex_step.y);
			curMean += fetch_cur(uv);
			prevMean += fetch_hist(uv);
		}
	}
	curMean *= 1.0 / 16.0;
	prevMean *= 1.0 / 16.0;
	float diff = abs(curMean - prevMean);

	/*
	 * Expected noise of the block mean from the sensor noise model: the
	 * variance grows with the signal (shot noise), and the mean of 16
	 * pixels has a sixteenth of the variance of one.
	 */
	float signal = max(prevMean - black, 0.0);
	float sigma = sqrt((noise_a * signal + noise_b) / 16.0);
	float threshold = motion_k * sigma;

	float w = mix(alpha, 1.0, smoothstep(threshold, 1.5 * threshold, diff));
	float v = hist_valid > 0.5 ? mix(prev, cur, w) : cur;

	gl_FragColor = pack_hist(v);
}
