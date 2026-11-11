/*

Copyright (c) 2021, Dominic Szablewski - https://phoboslab.org
SPDX-License-Identifier: MIT


Test suite for qoi.h

Compile and run with:
	gcc qoitest.c -std=c99 -O2 -o qoitest && ./qoitest

*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define QOI_IMPLEMENTATION
#include "qoi.h"

#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)
#define ERROR(...) do { \
	printf("FAIL at line " TOSTRING(__LINE__) ": " __VA_ARGS__); \
	printf("\n"); \
	exit(1); \
} while (0)


/* A deterministic pseudo random number generator, so tests are reproducible. */
static unsigned int qoi_test_rng = 0x12345678;
static unsigned int qoi_test_rand(void) {
	qoi_test_rng ^= qoi_test_rng << 13;
	qoi_test_rng ^= qoi_test_rng >> 17;
	qoi_test_rng ^= qoi_test_rng << 5;
	return qoi_test_rng;
}


/* -----------------------------------------------------------------------------
Golden vectors

These pin the exact byte stream produced by the encoder. They were generated
with the reference implementation and guard against any change to the format
or to the choice of chunk for a given pixel. */

static const unsigned char qoi_golden_rgb_4x2[] = {
	0x71, 0x6f, 0x69, 0x66, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x02,
	0x03, 0x00, 0xc0, 0xaa, 0x88, 0xa1, 0x9a, 0x7f, 0xc1, 0xfe, 0xc8, 0x32,
	0x1e, 0xfe, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x01
};

static const unsigned char qoi_golden_rgba_2x2[] = {
	0x71, 0x6f, 0x69, 0x66, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02,
	0x04, 0x00, 0xff, 0x01, 0x02, 0x03, 0x04, 0xc0, 0xff, 0x05, 0x06, 0x07,
	0x08, 0xff, 0x09, 0x09, 0x09, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x01
};

static const unsigned char qoi_golden_rgba_1x1[] = {
	0x71, 0x6f, 0x69, 0x66, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
	0x04, 0x00, 0xfe, 0x2a, 0x2b, 0x2c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x01
};


static void test_golden(void) {
	/* 4x2 RGB. Exercises RUN, DIFF, LUMA, RGB, INDEX and the end marker. */
	static const unsigned char rgb_4x2[] = {
		0,0,0,  10,10,10,  12,11,13,  13,12,14,
		13,12,14,  13,12,14,  200,50,30,  0,0,0
	};
	/* 2x2 RGBA. Exercises RGBA and a change of alpha. */
	static const unsigned char rgba_2x2[] = {
		1,2,3,4,  1,2,3,4,
		5,6,7,8,  9,9,9,255
	};
	/* 1x1 RGBA with an opaque pixel. */
	static const unsigned char rgba_1x1[] = { 42, 43, 44, 255 };

	struct {
		const char *name;
		const unsigned char *pixels;
		int width, height, channels;
		const unsigned char *expected;
		int expected_len;
	} cases[] = {
		{"rgb 4x2", rgb_4x2, 4, 2, 3, qoi_golden_rgb_4x2, sizeof(qoi_golden_rgb_4x2)},
		{"rgba 2x2", rgba_2x2, 2, 2, 4, qoi_golden_rgba_2x2, sizeof(qoi_golden_rgba_2x2)},
		{"rgba 1x1", rgba_1x1, 1, 1, 4, qoi_golden_rgba_1x1, sizeof(qoi_golden_rgba_1x1)},
	};

	for (unsigned int i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		qoi_desc desc = {
			.width = cases[i].width,
			.height = cases[i].height,
			.channels = cases[i].channels,
			.colorspace = QOI_SRGB
		};
		int len = 0;
		void *encoded = qoi_encode(cases[i].pixels, &desc, &len);
		if (!encoded) {
			ERROR("encode returned NULL for %s", cases[i].name);
		}
		if (len != cases[i].expected_len) {
			ERROR(
				"%s: expected %d bytes, got %d",
				cases[i].name, cases[i].expected_len, len
			);
		}
		if (memcmp(encoded, cases[i].expected, len) != 0) {
			ERROR("%s: encoded bytes differ from golden vector", cases[i].name);
		}
		free(encoded);
	}
}


/* -----------------------------------------------------------------------------
Roundtrip

For every combination of input channels, output channels and image content,
encoding followed by decoding must reproduce the original pixels. */

static void test_roundtrip(void) {
	static const int sizes[][2] = {
		{1, 1}, {1, 7}, {7, 1}, {2, 2}, {3, 5}, {16, 16}, {33, 17}
	};

	for (unsigned int s = 0; s < sizeof(sizes) / sizeof(sizes[0]); s++) {
		int w = sizes[s][0];
		int h = sizes[s][1];

		for (int channels = 3; channels <= 4; channels++) {
			for (int content = 0; content < 6; content++) {
				int px_len = w * h * channels;
				unsigned char *pixels = (unsigned char *)malloc(px_len);
				if (!pixels) {
					ERROR("malloc failed");
				}

				for (int i = 0; i < px_len; i++) {
					switch (content) {
						case 0: pixels[i] = 0; break;
						case 1: pixels[i] = 255; break;
						case 2: pixels[i] = (unsigned char)qoi_test_rand(); break;
						case 3: pixels[i] = (unsigned char)(i & 0xff); break;
						case 4: pixels[i] = (unsigned char)(((i / channels) & 1) ? 255 : 0); break;
						default: pixels[i] = (unsigned char)((i % 7 == 0) ? qoi_test_rand() : 42); break;
					}
				}

				/* Give some images a varying alpha channel to exercise QOI_OP_RGBA. */
				if (channels == 4 && (content & 1)) {
					for (int p = 0; p < w * h; p++) {
						pixels[p * 4 + 3] = (unsigned char)(p * 37);
					}
				}

				qoi_desc desc = {
					.width = w,
					.height = h,
					.channels = channels,
					.colorspace = QOI_SRGB
				};
				int encoded_len = 0;
				void *encoded = qoi_encode(pixels, &desc, &encoded_len);
				if (!encoded) {
					ERROR("encode failed for %dx%d x%d content %d", w, h, channels, content);
				}

				for (int out_channels = 0; out_channels <= 4; out_channels++) {
					if (out_channels == 1 || out_channels == 2) {
						continue;
					}

					qoi_desc decoded_desc;
					void *decoded = qoi_decode(
						encoded, encoded_len, &decoded_desc, out_channels
					);
					if (!decoded) {
						ERROR("decode failed for %dx%d x%d content %d out %d",
							w, h, channels, content, out_channels);
					}

					int expected_channels = out_channels ? out_channels : channels;
					if (
						decoded_desc.width != (unsigned int)w ||
						decoded_desc.height != (unsigned int)h ||
						decoded_desc.channels != channels
					) {
						ERROR("decoded description mismatch");
					}

					if (out_channels == 0 || out_channels == channels) {
						if (memcmp(decoded, pixels, px_len) != 0) {
							ERROR("roundtrip mismatch for %dx%d x%d content %d",
								w, h, channels, content);
						}
					}
					else {
						/* Decoding to a different channel count must match
						what the encoder put in: RGB bytes are preserved and
						alpha is dropped or set to opaque. */
						for (int p = 0; p < w * h; p++) {
							for (int c = 0; c < 3; c++) {
								unsigned char got = ((unsigned char *)decoded)[p * expected_channels + c];
								unsigned char want = pixels[p * channels + c];
								if (got != want) {
									ERROR("channel conversion mismatch");
								}
							}
							if (expected_channels == 4 && channels == 3) {
								if (((unsigned char *)decoded)[p * 4 + 3] != 255) {
									ERROR("expected opaque alpha when converting RGB to RGBA");
								}
							}
						}
					}

					free(decoded);
				}

				free(encoded);
				free(pixels);
			}
		}
	}
}


/* -----------------------------------------------------------------------------
Invalid input

None of the public functions may crash on malformed input; they must return
NULL (or 0 for qoi_write). */

static void test_invalid(void) {
	qoi_desc desc = { .width = 0, .height = 0, .channels = 4, .colorspace = QOI_SRGB };
	unsigned char pixel[4] = {0, 0, 0, 0};
	int len = 0;

	/* Encoding: invalid parameters */
	if (qoi_encode(NULL, &desc, &len) != NULL) { ERROR("encode NULL data"); }
	if (qoi_encode(pixel, NULL, &len) != NULL) { ERROR("encode NULL desc"); }
	if (qoi_encode(pixel, &desc, NULL) != NULL) { ERROR("encode NULL out_len"); }

	qoi_desc d;
	d = (qoi_desc){ .width = 0, .height = 1, .channels = 4, .colorspace = QOI_SRGB };
	if (qoi_encode(pixel, &d, &len) != NULL) { ERROR("encode zero width"); }
	d = (qoi_desc){ .width = 1, .height = 0, .channels = 4, .colorspace = QOI_SRGB };
	if (qoi_encode(pixel, &d, &len) != NULL) { ERROR("encode zero height"); }
	d = (qoi_desc){ .width = 1, .height = 1, .channels = 2, .colorspace = QOI_SRGB };
	if (qoi_encode(pixel, &d, &len) != NULL) { ERROR("encode bad channels"); }
	d = (qoi_desc){ .width = 1, .height = 1, .channels = 4, .colorspace = 2 };
	if (qoi_encode(pixel, &d, &len) != NULL) { ERROR("encode bad colorspace"); }

	/* Decoding: invalid parameters */
	qoi_desc out;
	if (qoi_decode(NULL, 0, &out, 0) != NULL) { ERROR("decode NULL data"); }
	if (qoi_decode(pixel, 4, NULL, 0) != NULL) { ERROR("decode NULL desc"); }
	if (qoi_decode(pixel, 4, &out, 2) != NULL) { ERROR("decode bad channels"); }
	if (qoi_decode(pixel, 4, &out, 0) != NULL) { ERROR("decode short input"); }

	/* Decoding: truncated stream with a valid header must not crash. */
	unsigned char truncated[QOI_HEADER_SIZE + 4] = {
		'q', 'o', 'i', 'f',
		0, 0, 0, 1, /* width */
		0, 0, 0, 1, /* height */
		4, 0
	};
	void *decoded = qoi_decode(truncated, sizeof(truncated), &out, 4);
	free(decoded); /* may be NULL; must not crash */

	/* Bad magic. */
	truncated[0] = 'x';
	decoded = qoi_decode(truncated, sizeof(truncated), &out, 4);
	if (decoded != NULL) { ERROR("decode bad magic should fail"); }
	free(decoded);
}


int main(void) {
	test_golden();
	test_roundtrip();
	test_invalid();

	printf("All tests passed.\n");
	return 0;
}
