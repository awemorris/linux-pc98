/*
 * Boots BeUI image decoders
 * Copyright (C) 2026 Awe Morris
 * SPDX-License-Identifier: Zlib
 *
 * BMP is used instead of PNG so the pre-boot environment does not require a
 * DEFLATE implementation.  The decoder is freestanding and allocation-free.
 */

#include "boot98-beui-image.h"

#include <limits.h>
#include <stdint.h>
#include <string.h>

struct bmp_layout {
	const uint8_t *bytes;
	size_t size;
	size_t data_offset;
	size_t source_stride;
	size_t output_stride;
	size_t output_size;
	size_t palette_offset;
	unsigned palette_size;
	unsigned width;
	unsigned height;
	unsigned bits_per_pixel;
	int top_down;
	enum boot98_beui_image_format format;
};

static uint16_t
read_u16(const uint8_t *bytes)
{
	return (uint16_t)(bytes[0] | (uint16_t)bytes[1] << 8);
}

static uint32_t
read_u32(const uint8_t *bytes)
{
	return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 |
	       (uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24;
}

static int32_t
read_s32(const uint8_t *bytes)
{
	return (int32_t)read_u32(bytes);
}

static int
add_overflows(size_t left, size_t right)
{
	return left > SIZE_MAX - right;
}

static int
multiply_overflows(size_t left, size_t right)
{
	return left != 0 && right > SIZE_MAX / left;
}

static int
parse_layout(const void *data, size_t size, struct bmp_layout *layout)
{
	const uint8_t *bytes = data;
	uint32_t dib_size;
	uint32_t data_offset;
	uint32_t colors_used;
	int32_t signed_width;
	int32_t signed_height;
	size_t row_bits;
	size_t palette_end;
	size_t source_bytes;
	unsigned bytes_per_pixel;

	if (bytes == NULL || layout == NULL || size < 54U || bytes[0] != 'B' ||
	    bytes[1] != 'M')
		return 0;
	dib_size = read_u32(bytes + 14);
	data_offset = read_u32(bytes + 10);
	if (dib_size < 40U || add_overflows(14U, dib_size) ||
	    14U + dib_size > size || data_offset > size)
		return 0;
	signed_width = read_s32(bytes + 18);
	signed_height = read_s32(bytes + 22);
	if (signed_width <= 0 || signed_height == 0 || signed_height == INT_MIN ||
	    read_u16(bytes + 26) != 1U || read_u32(bytes + 30) != 0U)
		return 0;
	memset(layout, 0, sizeof(*layout));
	layout->bytes = bytes;
	layout->size = size;
	layout->data_offset = data_offset;
	layout->width = (unsigned)signed_width;
	layout->height = signed_height < 0 ? (unsigned)-signed_height :
		(unsigned)signed_height;
	layout->top_down = signed_height < 0;
	layout->bits_per_pixel = read_u16(bytes + 28);
	switch (layout->bits_per_pixel) {
	case 1:
	case 4:
	case 8:
		layout->format = BOOT98_BEUI_IMAGE_INDEX8;
		bytes_per_pixel = 1;
		colors_used = read_u32(bytes + 46);
		layout->palette_size = colors_used != 0 ? colors_used :
			1U << layout->bits_per_pixel;
		if (layout->palette_size == 0 || layout->palette_size > 256U)
			return 0;
		layout->palette_offset = 14U + dib_size;
		if (multiply_overflows(layout->palette_size, 4U) ||
		    add_overflows(layout->palette_offset,
				  (size_t)layout->palette_size * 4U))
			return 0;
		palette_end = layout->palette_offset +
			(size_t)layout->palette_size * 4U;
		if (palette_end > data_offset || palette_end > size)
			return 0;
		break;
	case 24:
		layout->format = BOOT98_BEUI_IMAGE_RGB24;
		bytes_per_pixel = 3;
		break;
	default:
		return 0;
	}
	if (multiply_overflows(layout->width, layout->bits_per_pixel))
		return 0;
	row_bits = (size_t)layout->width * layout->bits_per_pixel;
	if (add_overflows(row_bits, 31U))
		return 0;
	layout->source_stride = ((row_bits + 31U) / 32U) * 4U;
	if (multiply_overflows(layout->width, bytes_per_pixel))
		return 0;
	layout->output_stride = (size_t)layout->width * bytes_per_pixel;
	if (multiply_overflows(layout->source_stride, layout->height) ||
	    multiply_overflows(layout->output_stride, layout->height))
		return 0;
	source_bytes = layout->source_stride * layout->height;
	layout->output_size = layout->output_stride * layout->height;
	if (add_overflows(data_offset, source_bytes) ||
	    data_offset + source_bytes > size)
		return 0;
	return 1;
}

int
boot98_beui_bmp_measure(const void *data, size_t size,
			enum boot98_beui_image_format *format,
			unsigned *width, unsigned *height, size_t *pixel_bytes)
{
	struct bmp_layout layout;

	if (format == NULL || width == NULL || height == NULL ||
	    pixel_bytes == NULL || !parse_layout(data, size, &layout))
		return 0;
	*format = layout.format;
	*width = layout.width;
	*height = layout.height;
	*pixel_bytes = layout.output_size;
	return 1;
}

int
boot98_beui_bmp_decode(const void *data, size_t size, void *pixel_storage,
		       size_t pixel_capacity, struct boot98_beui_image *image)
{
	struct bmp_layout layout;
	uint8_t *output = pixel_storage;
	unsigned y;

	if (output == NULL || image == NULL ||
	    !parse_layout(data, size, &layout) ||
	    pixel_capacity < layout.output_size)
		return 0;
	memset(image, 0, sizeof(*image));
	image->format = layout.format;
	image->width = layout.width;
	image->height = layout.height;
	image->stride = layout.output_stride;
	image->pixels = output;
	image->palette_size = layout.palette_size;
	for (y = 0; y < layout.palette_size; y++) {
		const uint8_t *entry = layout.bytes + layout.palette_offset +
			(size_t)y * 4U;

		image->palette[y] = (uint32_t)entry[2] << 16 |
			(uint32_t)entry[1] << 8 | entry[0];
	}
	for (y = 0; y < layout.height; y++) {
		unsigned source_y = layout.top_down ? y : layout.height - 1U - y;
		const uint8_t *source = layout.bytes + layout.data_offset +
			(size_t)source_y * layout.source_stride;
		uint8_t *destination = output + (size_t)y * layout.output_stride;
		unsigned x;

		if (layout.bits_per_pixel == 1U) {
			for (x = 0; x < layout.width; x++)
				destination[x] = (uint8_t)(
					(source[x >> 3] >> (7U - (x & 7U))) & 1U);
		} else if (layout.bits_per_pixel == 4U) {
			for (x = 0; x < layout.width; x++)
				destination[x] = (uint8_t)(
					(source[x >> 1] >> ((x & 1U) ? 0U : 4U)) &
					0x0fU);
		} else if (layout.bits_per_pixel == 8U) {
			memcpy(destination, source, layout.width);
		} else {
			for (x = 0; x < layout.width; x++) {
				destination[(size_t)x * 3U] = source[(size_t)x * 3U + 2U];
				destination[(size_t)x * 3U + 1U] = source[(size_t)x * 3U + 1U];
				destination[(size_t)x * 3U + 2U] = source[(size_t)x * 3U];
			}
		}
	}
	return 1;
}
