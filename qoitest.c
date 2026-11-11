/*

Copyright (c) 2021, Dominic Szablewski - https://phoboslab.org
SPDX-License-Identifier: MIT


Test suite for qoi.h

Compile and run with:
	make test

*/


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define QOI_IMPLEMENTATION
#include "qoi.h"


#define TEST_ASSERT(EXPR) \
	do { \
		if (!(EXPR)) { \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #EXPR); \
			exit(1); \
		} \
	} while (0)


/* Fill an RGB or RGBA buffer with a deterministic pattern that exercises all
chunk types (runs, index hits, diffs, luma and full RGB/RGBA writes). */
static void fill_pixels(unsigned char *pixels, int w, int h, int channels, int seed) {
	int x, y;
	for (y = 0; y < h; y++) {
		for (x = 0; x < w; x++) {
			int i = (y * w + x) * channels;
			pixels[i + 0] = (unsigned char)((x * 3 + y + seed * 7) & 0xff);
			pixels[i + 1] = (unsigned char)((y * 5 + x / 3 + seed * 13) & 0xff);
			pixels[i + 2] = (unsigned char)(((x >> 3) * 8 + (y >> 5) * 4 + seed * 29) & 0xff);
			if (channels == 4) {
				pixels[i + 3] = (unsigned char)((x + y + seed * 11) & 0xff);
			}
		}
	}

	/* A solid block to force QOI_OP_RUN chunks. */
	for (y = 0; y < h / 2 && y < 8; y++) {
		for (x = 0; x < w; x++) {
			int i = (y * w + x) * channels;
			pixels[i + 0] = 42;
			pixels[i + 1] = 42;
			pixels[i + 2] = 42;
			if (channels == 4) {
				pixels[i + 3] = 255;
			}
		}
	}
}


static void test_encode_to_matches_encode(void) {
	const int w = 33, h = 17;
	int channels;
	for (channels = 3; channels <= 4; channels++) {
		unsigned char pixels[33 * 17 * 4];
		qoi_desc desc = {w, h, (unsigned char)channels, QOI_SRGB};
		int alloc_len, to_len, size;
		void *alloc_encoded;
		unsigned char *to_encoded;

		fill_pixels(pixels, w, h, channels, channels);

		size = qoi_encode_size(&desc);
		TEST_ASSERT(size == w * h * (channels + 1) + 14 + 8);

		alloc_encoded = qoi_encode(pixels, &desc, &alloc_len);
		TEST_ASSERT(alloc_encoded != NULL);

		to_encoded = (unsigned char *)malloc(size);
		TEST_ASSERT(to_encoded != NULL);

		to_len = 0;
		TEST_ASSERT(qoi_encode_to(pixels, &desc, to_encoded, size, &to_len) == alloc_len);
		TEST_ASSERT(to_len == alloc_len);
		TEST_ASSERT(memcmp(to_encoded, alloc_encoded, alloc_len) == 0);

		/* A buffer one byte too small must be rejected. */
		TEST_ASSERT(qoi_encode_to(pixels, &desc, to_encoded, size - 1, &to_len) == 0);

		free(alloc_encoded);
		free(to_encoded);
	}
}


static void test_encode_invalid(void) {
	unsigned char pixels[16] = {0};
	unsigned char out[256];
	int len;

	TEST_ASSERT(qoi_encode_size(NULL) == 0);
	TEST_ASSERT(qoi_encode_size(&(qoi_desc){0, 1, 4, QOI_SRGB}) == 0);
	TEST_ASSERT(qoi_encode_size(&(qoi_desc){1, 0, 4, QOI_SRGB}) == 0);
	TEST_ASSERT(qoi_encode_size(&(qoi_desc){1, 1, 2, QOI_SRGB}) == 0);
	TEST_ASSERT(qoi_encode_size(&(qoi_desc){1, 1, 5, QOI_SRGB}) == 0);
	TEST_ASSERT(qoi_encode_size(&(qoi_desc){1, 1, 4, 2}) == 0);

	TEST_ASSERT(qoi_encode_to(NULL, &(qoi_desc){1, 1, 4, QOI_SRGB}, out, sizeof(out), &len) == 0);
	TEST_ASSERT(qoi_encode_to(pixels, &(qoi_desc){1, 1, 4, QOI_SRGB}, NULL, sizeof(out), &len) == 0);
	TEST_ASSERT(qoi_encode_to(pixels, &(qoi_desc){1, 1, 4, QOI_SRGB}, out, sizeof(out), NULL) == 0);
	TEST_ASSERT(qoi_encode(NULL, &(qoi_desc){1, 1, 4, QOI_SRGB}, &len) == NULL);
}


static void test_decode_to_matches_decode(void) {
	const int w = 29, h = 23;
	int channels;
	for (channels = 3; channels <= 4; channels++) {
		int out_channels;
		for (out_channels = 0; out_channels <= 4; out_channels++) {
			unsigned char pixels[29 * 23 * 4];
			qoi_desc desc = {w, h, (unsigned char)channels, QOI_LINEAR};
			qoi_desc alloc_desc, to_desc;
			int encoded_len, effective_channels;
			void *encoded;
			void *alloc_decoded;
			unsigned char *to_decoded;
			int px_len;

			if (out_channels == 1 || out_channels == 2) {
				continue;
			}

			fill_pixels(pixels, w, h, channels, channels * 3 + out_channels);
			encoded = qoi_encode(pixels, &desc, &encoded_len);
			TEST_ASSERT(encoded != NULL);

			alloc_decoded = qoi_decode(encoded, encoded_len, &alloc_desc, out_channels);
			TEST_ASSERT(alloc_decoded != NULL);

			effective_channels = out_channels ? out_channels : channels;
			px_len = w * h * effective_channels;
			to_decoded = (unsigned char *)malloc(px_len);
			TEST_ASSERT(to_decoded != NULL);

			TEST_ASSERT(qoi_decode_to(encoded, encoded_len, &to_desc, out_channels, to_decoded) == to_decoded);
			TEST_ASSERT(to_desc.width == alloc_desc.width);
			TEST_ASSERT(to_desc.height == alloc_desc.height);
			TEST_ASSERT(to_desc.channels == alloc_desc.channels);
			TEST_ASSERT(to_desc.colorspace == alloc_desc.colorspace);
			TEST_ASSERT(memcmp(to_decoded, alloc_decoded, px_len) == 0);

			/* Compare against the original pixels when no conversion happens. */
			if (effective_channels == channels) {
				TEST_ASSERT(memcmp(to_decoded, pixels, px_len) == 0);
			}

			free(encoded);
			free(alloc_decoded);
			free(to_decoded);
		}
	}
}


static void test_decode_to_invalid(void) {
	unsigned char pixels[4] = {0};
	unsigned char out[64];
	qoi_desc desc;
	unsigned char bad_header[22];

	memset(bad_header, 0, sizeof(bad_header));

	TEST_ASSERT(qoi_decode_to(NULL, 22, &desc, 4, out) == NULL);
	TEST_ASSERT(qoi_decode_to(bad_header, 22, NULL, 4, out) == NULL);
	TEST_ASSERT(qoi_decode_to(bad_header, 22, &desc, 4, NULL) == NULL);
	TEST_ASSERT(qoi_decode_to(bad_header, 22, &desc, 2, out) == NULL);
	TEST_ASSERT(qoi_decode_to(bad_header, 22, &desc, 4, out) == NULL);
	/* Too short to even hold a header and the end marker. */
	TEST_ASSERT(qoi_decode_to(pixels, sizeof(pixels), &desc, 4, out) == NULL);
}


/* The decoder must never write more than width * height * channels bytes into
the caller supplied buffer. */
static void test_decode_to_no_overflow(void) {
	const int w = 16, h = 16;
	unsigned char pixels[16 * 16 * 4];
	qoi_desc desc = {w, h, 4, QOI_SRGB};
	int encoded_len, i;
	void *encoded;
	unsigned char *buffer;
	const int guard = 64;
	const unsigned char guard_byte = 0xa5;

	fill_pixels(pixels, w, h, 4, 7);
	encoded = qoi_encode(pixels, &desc, &encoded_len);
	TEST_ASSERT(encoded != NULL);

	buffer = (unsigned char *)malloc(w * h * 4 + guard * 2);
	TEST_ASSERT(buffer != NULL);
	memset(buffer, guard_byte, w * h * 4 + guard * 2);

	TEST_ASSERT(qoi_decode_to(encoded, encoded_len, &desc, 4, buffer + guard));

	for (i = 0; i < guard; i++) {
		TEST_ASSERT(buffer[i] == guard_byte);
		TEST_ASSERT(buffer[guard + w * h * 4 + i] == guard_byte);
	}

	free(encoded);
	free(buffer);
}


/* Regression test for issue #317: processing a large number of small images
with reused buffers must produce exactly the same result as the per-image
allocating API. */
static void test_reuse_buffers_many_small_images(void) {
	const int w = 64, h = 64, channels = 4;
	const int count = 100;
	qoi_desc desc = {w, h, channels, QOI_SRGB};
	int encoded_capacity = qoi_encode_size(&desc);
	unsigned char *encoded = (unsigned char *)malloc(encoded_capacity);
	unsigned char *pixels = (unsigned char *)malloc(w * h * channels);
	unsigned char *decoded = (unsigned char *)malloc(w * h * channels);
	int i;

	TEST_ASSERT(encoded != NULL && pixels != NULL && decoded != NULL);

	for (i = 0; i < count; i++) {
		int encoded_len, alloc_len;
		void *alloc_encoded, *alloc_decoded;
		qoi_desc decoded_desc;

		fill_pixels(pixels, w, h, channels, i);

		TEST_ASSERT(qoi_encode_to(pixels, &desc, encoded, encoded_capacity, &encoded_len) == encoded_len);
		TEST_ASSERT(qoi_decode_to(encoded, encoded_len, &decoded_desc, channels, decoded) == decoded);
		TEST_ASSERT(memcmp(decoded, pixels, w * h * channels) == 0);

		/* Cross check with the allocating API. */
		alloc_encoded = qoi_encode(pixels, &desc, &alloc_len);
		TEST_ASSERT(alloc_encoded != NULL);
		TEST_ASSERT(alloc_len == encoded_len);
		TEST_ASSERT(memcmp(alloc_encoded, encoded, encoded_len) == 0);

		alloc_decoded = qoi_decode(encoded, encoded_len, &decoded_desc, channels);
		TEST_ASSERT(alloc_decoded != NULL);
		TEST_ASSERT(memcmp(alloc_decoded, decoded, w * h * channels) == 0);

		free(alloc_encoded);
		free(alloc_decoded);
	}

	free(encoded);
	free(pixels);
	free(decoded);
}


static void test_roundtrip_sizes(void) {
	const int sizes[][2] = {
		{1, 1}, {1, 62}, {62, 1}, {63, 63}, {1, 1000}, {1000, 1}, {256, 256}
	};
	int s;
	for (s = 0; s < (int)(sizeof(sizes) / sizeof(sizes[0])); s++) {
		int w = sizes[s][0], h = sizes[s][1];
		int channels;
		for (channels = 3; channels <= 4; channels++) {
			qoi_desc desc = {w, h, (unsigned char)channels, QOI_SRGB};
			qoi_desc decoded_desc;
			unsigned char *pixels = (unsigned char *)malloc(w * h * channels);
			unsigned char *decoded;
			int encoded_len;
			void *encoded;

			TEST_ASSERT(pixels != NULL);
			fill_pixels(pixels, w, h, channels, s + channels);

			encoded = qoi_encode(pixels, &desc, &encoded_len);
			TEST_ASSERT(encoded != NULL);

			decoded = (unsigned char *)qoi_decode(encoded, encoded_len, &decoded_desc, channels);
			TEST_ASSERT(decoded != NULL);
			TEST_ASSERT(decoded_desc.width == (unsigned int)w);
			TEST_ASSERT(decoded_desc.height == (unsigned int)h);
			TEST_ASSERT(memcmp(decoded, pixels, w * h * channels) == 0);

			free(pixels);
			free(encoded);
			free(decoded);
		}
	}
}


static void test_stdio_roundtrip(void) {
	const char *path = "qoitest_tmp.qoi";
	const int w = 32, h = 32;
	unsigned char pixels[32 * 32 * 4];
	qoi_desc desc = {w, h, 4, QOI_SRGB};
	qoi_desc read_desc;
	int written;
	void *decoded;

	fill_pixels(pixels, w, h, 4, 99);

	written = qoi_write(path, pixels, &desc);
	TEST_ASSERT(written > 0);

	decoded = qoi_read(path, &read_desc, 4);
	TEST_ASSERT(decoded != NULL);
	TEST_ASSERT(read_desc.width == (unsigned int)w);
	TEST_ASSERT(read_desc.height == (unsigned int)h);
	TEST_ASSERT(memcmp(decoded, pixels, w * h * 4) == 0);

	free(decoded);
	remove(path);
}


int main(void) {
	test_encode_to_matches_encode();
	test_encode_invalid();
	test_decode_to_matches_decode();
	test_decode_to_invalid();
	test_decode_to_no_overflow();
	test_reuse_buffers_many_small_images();
	test_roundtrip_sizes();
	test_stdio_roundtrip();

	printf("all tests passed\n");
	return 0;
}
