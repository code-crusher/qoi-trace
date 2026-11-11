/*

Copyright (c) 2021, Dominic Szablewski - https://phoboslab.org
SPDX-License-Identifier: MIT


Tests for qoi.h

Compile and run with:
	make test

These tests only cover the parts of qoi that are part of the file format
specification: the colour hash / index behaviour and lossless en-/decoding.
They deliberately do not depend on any image files or external libraries.

*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define QOI_IMPLEMENTATION
#include "qoi.h"


static int tests_run = 0;
static int tests_failed = 0;

#define CHECK(COND) do { \
	tests_run++; \
	if (!(COND)) { \
		tests_failed++; \
		printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #COND); \
	} \
} while (0)


/* Build an image of the given size from a callback that returns the grey
value of a pixel. The image is filled in the given number of channels. */
typedef unsigned char (*grey_fn)(int x, int y, int *alpha);

static unsigned char *make_grey_image(
	int w, int h, int channels, grey_fn fn
) {
	unsigned char *pixels = malloc((size_t)w * h * channels);
	for (int y = 0; y < h; y++) {
		for (int x = 0; x < w; x++) {
			int alpha = 255;
			unsigned char v = fn(x, y, &alpha);
			unsigned char *px = pixels + ((size_t)y * w + x) * channels;
			px[0] = v;
			px[1] = v;
			px[2] = v;
			if (channels == 4) {
				px[3] = (unsigned char)alpha;
			}
		}
	}
	return pixels;
}

/* Encode and decode the pixels, and check that the result is identical. */
static void check_roundtrip(const char *name, void *pixels, int w, int h, int channels) {
	qoi_desc desc = {
		.width = w,
		.height = h,
		.channels = channels,
		.colorspace = QOI_SRGB
	};
	int len = 0;
	void *encoded = qoi_encode(pixels, &desc, &len);

	tests_run++;
	if (!encoded) {
		tests_failed++;
		printf("FAIL %s: qoi_encode returned NULL\n", name);
		return;
	}

	qoi_desc decoded_desc;
	void *decoded = qoi_decode(encoded, len, &decoded_desc, channels);

	tests_run++;
	if (!decoded) {
		tests_failed++;
		printf("FAIL %s: qoi_decode returned NULL\n", name);
		free(encoded);
		return;
	}

	tests_run++;
	if (
		decoded_desc.width != desc.width ||
		decoded_desc.height != desc.height ||
		decoded_desc.channels != desc.channels
	) {
		tests_failed++;
		printf("FAIL %s: decoded description does not match\n", name);
	}

	tests_run++;
	if (memcmp(pixels, decoded, (size_t)w * h * channels) != 0) {
		tests_failed++;
		printf("FAIL %s: roundtrip pixel mismatch\n", name);
	}

	free(decoded);
	free(encoded);
}

/* Count the chunk types in an encoded qoi image. */
typedef struct {
	int index;
	int diff;
	int luma;
	int run;
	int rgb;
	int rgba;
} chunk_counts;

static chunk_counts count_chunks(const void *data, int size) {
	const unsigned char *bytes = data;
	chunk_counts counts = {0};
	int p = QOI_HEADER_SIZE;
	int end = size - (int)sizeof(qoi_padding);

	while (p < end) {
		int b = bytes[p];
		if (b == QOI_OP_RGB) {
			counts.rgb++;
			p += 4;
		}
		else if (b == QOI_OP_RGBA) {
			counts.rgba++;
			p += 5;
		}
		else if ((b & QOI_MASK_2) == QOI_OP_INDEX) {
			counts.index++;
			p += 1;
		}
		else if ((b & QOI_MASK_2) == QOI_OP_LUMA) {
			counts.luma++;
			p += 2;
		}
		else if ((b & QOI_MASK_2) == QOI_OP_RUN) {
			counts.run++;
			p += 1;
		}
		else {
			counts.diff++;
			p += 1;
		}
	}
	return counts;
}


/* -----------------------------------------------------------------------------
The 52 shades of grey palette from issue #341. */

static unsigned char grey_palette(int x, int y, int *alpha) {
	(void)alpha;
	return (unsigned char)(((x + y) % 52) * 5);
}

static void test_grey_palette_hash(void) {
	/* All 52 shades have to map to distinct index positions. The hash is
	(15 * r + 11 * a) % 64 for greys, and 15 is coprime to 64, so this holds
	for any shade that differs modulo 64. */
	int seen[64] = {0};
	int shades = 0;
	for (int v = 0; v <= 255; v += 5) {
		qoi_rgba_t px;
		px.rgba.r = px.rgba.g = px.rgba.b = (unsigned char)v;
		px.rgba.a = 255;
		int pos = QOI_COLOR_HASH(px) & 63;
		CHECK(pos >= 0 && pos < 64);
		CHECK(seen[pos] == 0);
		seen[pos] = 1;
		shades++;
	}
	CHECK(shades == 52);
}

static void test_grey_palette_roundtrip(void) {
	int w = 52;
	int h = 52;
	for (int channels = 3; channels <= 4; channels++) {
		unsigned char *pixels = make_grey_image(w, h, channels, grey_palette);
		check_roundtrip("grey palette", pixels, w, h, channels);
		free(pixels);
	}
}

static void test_grey_palette_uses_index(void) {
	/* An image drawn from the palette should be encoded mostly with
	QOI_OP_INDEX, because every shade stays in the index table. */
	int w = 52;
	int h = 52;
	unsigned char *pixels = make_grey_image(w, h, 3, grey_palette);
	qoi_desc desc = {.width = w, .height = h, .channels = 3, .colorspace = QOI_SRGB};
	int len = 0;
	void *encoded = qoi_encode(pixels, &desc, &len);
	CHECK(encoded != NULL);
	if (encoded) {
		chunk_counts counts = count_chunks(encoded, len);
		/* The very first pixel cannot be an index, and the grey ramp
		alternates between index and luma chunks. Everything else is an
		index lookup. */
		CHECK(counts.index > w * h / 2);
		CHECK(counts.rgb == 0);
		CHECK(counts.rgba == 0);
	}
	free(encoded);
	free(pixels);
}

static void test_grey_palette_with_alpha(void) {
	/* The same 52 shades are collision free for every alpha value, because
	the alpha term (11 * a) only shifts the position. */
	int seen[64];
	for (int a = 0; a <= 255; a++) {
		memset(seen, 0, sizeof(seen));
		for (int v = 0; v <= 255; v += 5) {
			qoi_rgba_t px;
			px.rgba.r = px.rgba.g = px.rgba.b = (unsigned char)v;
			px.rgba.a = (unsigned char)a;
			int pos = QOI_COLOR_HASH(px) & 63;
			CHECK(seen[pos] == 0);
			seen[pos] = 1;
		}
	}
}


/* -----------------------------------------------------------------------------
Roundtrips for grey images in general, including alpha and index collisions. */

static unsigned char grey_ramp(int x, int y, int *alpha) {
	(void)y;
	(void)alpha;
	return (unsigned char)x;
}

static unsigned char grey_with_alpha(int x, int y, int *alpha) {
	(void)y;
	*alpha = (x & 1) ? 255 : 0;
	return (unsigned char)(x * 3);
}

static void test_grey_roundtrips(void) {
	/* A full 256 level grey ramp shares index positions between shades that
	are equal modulo 64. The roundtrip still has to be lossless. */
	int w = 256;
	int h = 4;
	unsigned char *pixels = make_grey_image(w, h, 3, grey_ramp);
	check_roundtrip("grey ramp", pixels, w, h, 3);
	free(pixels);

	pixels = make_grey_image(w, h, 4, grey_ramp);
	check_roundtrip("grey ramp with alpha", pixels, w, h, 4);
	free(pixels);

	pixels = make_grey_image(w, h, 4, grey_with_alpha);
	check_roundtrip("grey ramp with alternating alpha", pixels, w, h, 4);
	free(pixels);
}

static void test_grey_collisions(void) {
	/* 0 and 128 are both 0 modulo 64, so they hash to the same index
	position. Alternate them to force index table overwrites. */
	int w = 64;
	int h = 8;
	unsigned char *pixels = malloc((size_t)w * h * 3);
	for (int y = 0; y < h; y++) {
		for (int x = 0; x < w; x++) {
			unsigned char v = (unsigned char)((x & 1) ? 128 : 0);
			unsigned char *px = pixels + ((size_t)y * w + x) * 3;
			px[0] = px[1] = px[2] = v;
		}
	}
	check_roundtrip("colliding greys", pixels, w, h, 3);
	free(pixels);
}


/* -----------------------------------------------------------------------------
Roundtrips for images that mix index lookups with QOI_OP_RGB/LUMA chunks. The
decoder has to insert every decoded pixel into the index, so that a later
QOI_OP_INDEX returns the right colour. */

static void test_mixed_colour_roundtrip(void) {
	/* This image came out of a randomized search: it encodes with QOI_OP_RGB
	chunks followed by index lookups, and the decoder used to diverge from
	the encoder on the index table. */
	static const unsigned char pixels[] = {
		255,255,0, 17,200,90, 0,255,0, 200,100,50, 17,200,90, 255,0,0,
		250,10,240, 0,255,0, 200,100,50, 0,255,0, 255,0,0, 0,0,255,
		129,64,200, 0,0,255
	};
	check_roundtrip("mixed colours", (void *)pixels, 2, 7, 3);

	/* The same pattern in RGBA, so that QOI_OP_RGBA is exercised as well. */
	static const unsigned char pixels_rgba[] = {
		255,255,0,255, 17,200,90,0, 0,255,0,255, 200,100,50,128,
		17,200,90,0, 255,0,0,255, 250,10,240,7, 0,255,0,255,
		200,100,50,128, 0,255,0,255, 255,0,0,255, 0,0,255,200,
		129,64,200,1, 0,0,255,255
	};
	check_roundtrip("mixed colours with alpha", (void *)pixels_rgba, 2, 7, 4);
}

static void test_random_roundtrips(void) {
	/* Deterministic pseudo random images over a small colour alphabet,
	covering all chunk types. */
	static const unsigned char palette[][4] = {
		{255,0,0,255}, {0,255,0,255}, {0,0,255,255}, {200,100,50,255},
		{17,200,90,128}, {250,10,240,0}, {1,2,3,255}, {129,64,200,255},
		{0,0,0,255}, {0,0,0,0}, {5,5,5,255}, {128,128,128,255}
	};
	unsigned int seed = 1;
	for (int t = 0; t < 2000; t++) {
		seed = seed * 1103515245u + 12345u;
		int w = 1 + (int)((seed >> 16) % 8);
		seed = seed * 1103515245u + 12345u;
		int h = 1 + (int)((seed >> 16) % 8);
		int channels = (t & 1) ? 3 : 4;
		unsigned char pixels[64 * 4];
		for (int i = 0; i < w * h; i++) {
			seed = seed * 1103515245u + 12345u;
			const unsigned char *c = palette[(seed >> 16) % 12];
			memcpy(pixels + i * channels, c, channels);
		}
		check_roundtrip("random colours", pixels, w, h, channels);
	}
}


/* -----------------------------------------------------------------------------
The index hash has to agree between the encoder and the decoder. */

static void test_hash_matches_spec(void) {
	/* The documentation specifies (r * 3 + g * 5 + b * 7 + a * 11) % 64.
	The implementation uses & 63, which is equivalent for the (positive)
	result of the sum. Check a few values against the documented formula. */
	for (int r = 0; r <= 255; r += 17) {
		for (int g = 0; g <= 255; g += 29) {
			for (int b = 0; b <= 255; b += 31) {
				for (int a = 0; a <= 255; a += 37) {
					qoi_rgba_t px;
					px.rgba.r = (unsigned char)r;
					px.rgba.g = (unsigned char)g;
					px.rgba.b = (unsigned char)b;
					px.rgba.a = (unsigned char)a;
					CHECK((QOI_COLOR_HASH(px) & 63) == (r * 3 + g * 5 + b * 7 + a * 11) % 64);
				}
			}
		}
	}
}

static void test_grey_hash_formula(void) {
	/* For greys the hash reduces to (15 * r + 11 * a) % 64. */
	for (int v = 0; v <= 255; v++) {
		for (int a = 0; a <= 255; a += 51) {
			qoi_rgba_t px;
			px.rgba.r = px.rgba.g = px.rgba.b = (unsigned char)v;
			px.rgba.a = (unsigned char)a;
			CHECK((QOI_COLOR_HASH(px) & 63) == (15 * v + 11 * a) % 64);
		}
	}
}


/* -----------------------------------------------------------------------------
API error handling. */

static void test_invalid_parameters(void) {
	qoi_desc desc = {.width = 1, .height = 1, .channels = 3, .colorspace = QOI_SRGB};
	unsigned char pixel[3] = {0, 0, 0};
	int len = 0;

	CHECK(qoi_encode(NULL, &desc, &len) == NULL);
	CHECK(qoi_encode(pixel, NULL, &len) == NULL);
	CHECK(qoi_encode(pixel, &desc, NULL) == NULL);

	desc.width = 0;
	CHECK(qoi_encode(pixel, &desc, &len) == NULL);
	desc.width = 1;
	desc.channels = 2;
	CHECK(qoi_encode(pixel, &desc, &len) == NULL);
	desc.channels = 3;

	CHECK(qoi_decode(NULL, 64, &desc, 3) == NULL);
	CHECK(qoi_decode(pixel, 64, NULL, 3) == NULL);
	CHECK(qoi_decode(pixel, 64, &desc, 5) == NULL);
	CHECK(qoi_decode(pixel, 4, &desc, 3) == NULL);
}


int main(void) {
	test_hash_matches_spec();
	test_grey_hash_formula();
	test_grey_palette_hash();
	test_grey_palette_roundtrip();
	test_grey_palette_uses_index();
	test_grey_palette_with_alpha();
	test_grey_roundtrips();
	test_grey_collisions();
	test_mixed_colour_roundtrip();
	test_random_roundtrips();
	test_invalid_parameters();

	printf("%d checks, %d failed\n", tests_run, tests_failed);
	return tests_failed == 0 ? 0 : 1;
}
