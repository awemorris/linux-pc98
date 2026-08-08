/*
 * Boots BeUI BMP decoder host test
 * Copyright (C) 2026 Awe Morris
 * SPDX-License-Identifier: Zlib
 */

#include "boot98-beui-image.h"

#include <stdint.h>
#include <string.h>

static void
put16(uint8_t *destination, uint16_t value)
{
	destination[0] = (uint8_t)value;
	destination[1] = (uint8_t)(value >> 8);
}

static void
put32(uint8_t *destination, uint32_t value)
{
	destination[0] = (uint8_t)value;
	destination[1] = (uint8_t)(value >> 8);
	destination[2] = (uint8_t)(value >> 16);
	destination[3] = (uint8_t)(value >> 24);
}

static size_t
make_bmp(uint8_t *bmp, unsigned bits_per_pixel, int top_down)
{
	unsigned colors = bits_per_pixel <= 8 ? 1U << bits_per_pixel : 0;
	unsigned data_offset = 54U + colors * 4U;
	unsigned stride = ((3U * bits_per_pixel + 31U) / 32U) * 4U;
	unsigned source_y;

	memset(bmp, 0, 2048);
	bmp[0] = 'B';
	bmp[1] = 'M';
	put32(bmp + 2, data_offset + stride * 2U);
	put32(bmp + 10, data_offset);
	put32(bmp + 14, 40);
	put32(bmp + 18, 3);
	put32(bmp + 22, top_down ? (uint32_t)-2 : 2);
	put16(bmp + 26, 1);
	put16(bmp + 28, (uint16_t)bits_per_pixel);
	put32(bmp + 34, stride * 2U);
	put32(bmp + 46, colors);
	for (source_y = 0; source_y < colors; source_y++) {
		uint8_t *entry = bmp + 54U + source_y * 4U;

		entry[0] = (uint8_t)(source_y * 17U);
		entry[1] = (uint8_t)(source_y * 11U);
		entry[2] = (uint8_t)(source_y * 7U);
	}
	for (source_y = 0; source_y < 2; source_y++) {
		unsigned logical_y = top_down ? source_y : 1U - source_y;
		uint8_t *row = bmp + data_offset + source_y * stride;
		unsigned values[3];

		if (bits_per_pixel == 1U) {
			values[0] = logical_y == 0 ? 1U : 0U;
			values[1] = logical_y == 0 ? 0U : 1U;
			values[2] = logical_y == 0 ? 1U : 0U;
			row[0] = (uint8_t)(values[0] << 7 | values[1] << 6 |
					 values[2] << 5);
		} else if (bits_per_pixel == 4U) {
			values[0] = logical_y == 0 ? 1U : 3U;
			values[1] = 2U;
			values[2] = logical_y == 0 ? 3U : 1U;
			row[0] = (uint8_t)(values[0] << 4 | values[1]);
			row[1] = (uint8_t)(values[2] << 4);
		} else if (bits_per_pixel == 8U) {
			row[0] = (uint8_t)(logical_y == 0 ? 1U : 3U);
			row[1] = 2;
			row[2] = (uint8_t)(logical_y == 0 ? 3U : 1U);
		} else {
			static const uint8_t top[9] = {
				0, 0, 255, 0, 255, 0, 255, 0, 0,
			};
			static const uint8_t bottom[9] = {
				255, 255, 255, 0, 0, 0, 0x33, 0x22, 0x11,
			};

			memcpy(row, logical_y == 0 ? top : bottom, 9);
		}
	}
	return data_offset + stride * 2U;
}

static int
test_indexed(unsigned bits_per_pixel)
{
	uint8_t bmp[2048];
	uint8_t pixels[6];
	struct boot98_beui_image image;
	enum boot98_beui_image_format format;
	unsigned width;
	unsigned height;
	size_t pixel_bytes;
	size_t size = make_bmp(bmp, bits_per_pixel, 0);
	uint8_t expected[6];

	if (bits_per_pixel == 1U) {
		uint8_t values[6] = { 1, 0, 1, 0, 1, 0 };
		memcpy(expected, values, sizeof(expected));
	} else {
		uint8_t values[6] = { 1, 2, 3, 3, 2, 1 };
		memcpy(expected, values, sizeof(expected));
	}
	if (!boot98_beui_bmp_measure(bmp, size, &format, &width, &height,
				     &pixel_bytes) ||
	    format != BOOT98_BEUI_IMAGE_INDEX8 || width != 3 || height != 2 ||
	    pixel_bytes != sizeof(pixels) ||
	    !boot98_beui_bmp_decode(bmp, size, pixels, sizeof(pixels), &image) ||
	    image.stride != 3 || image.palette_size != (1U << bits_per_pixel) ||
	    memcmp(pixels, expected, sizeof(pixels)) != 0 ||
	    image.palette[1] != 0x00070b11U)
		return 0;
	return 1;
}

static int
test_rgb24(void)
{
	uint8_t bmp[2048];
	uint8_t pixels[18];
	struct boot98_beui_image image;
	static const uint8_t expected[18] = {
		255, 0, 0, 0, 255, 0, 0, 0, 255,
		255, 255, 255, 0, 0, 0, 0x11, 0x22, 0x33,
	};
	size_t size = make_bmp(bmp, 24, 1);

	return boot98_beui_bmp_decode(bmp, size, pixels, sizeof(pixels), &image) &&
	       image.format == BOOT98_BEUI_IMAGE_RGB24 && image.stride == 9 &&
	       image.palette_size == 0 &&
	       memcmp(pixels, expected, sizeof(pixels)) == 0;
}

int
main(void)
{
	uint8_t invalid[2048];
	enum boot98_beui_image_format format;
	unsigned width;
	unsigned height;
	size_t pixel_bytes;
	size_t size;

	if (!test_indexed(1) || !test_indexed(4) || !test_indexed(8) ||
	    !test_rgb24())
		return 1;
	size = make_bmp(invalid, 8, 0);
	put32(invalid + 30, 1);
	if (boot98_beui_bmp_measure(invalid, size, &format, &width, &height,
				    &pixel_bytes))
		return 2;
	put32(invalid + 30, 0);
	if (boot98_beui_bmp_measure(invalid, size - 1U, &format, &width, &height,
				    &pixel_bytes))
		return 3;
	return 0;
}
