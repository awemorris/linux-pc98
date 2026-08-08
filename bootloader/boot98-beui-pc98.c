/*
 * Boots BeUI NEC PC-9800 GDC safe-mode backend
 * Copyright (C) 2026 Awe Morris
 * Copyright (C) 1996-2024 Keiichi Tabata
 * SPDX-License-Identifier: Zlib
 *
 * Display sequencing is adapted from StratoHAL 98disp_gdc.c at commit
 * 76e909577bdf4629f11e473539b446a948fef830. This Boots version is altered
 * to preserve text VRAM and update only requested rectangles.
 */

#include "boot98-beui-pc98.h"

#include <string.h>

#define GDC_WIDTH 640U
#define GDC_HEIGHT 400U
#define GDC_STRIDE (GDC_WIDTH / 8U)

static uint8_t
native_in8(void *context, uint16_t port)
{
	uint8_t value;

	(void)context;
	__asm__ volatile ("inb %w1,%0" : "=a"(value) : "Nd"(port));
	return value;
}

static void
native_out8(void *context, uint16_t port, uint8_t value)
{
	(void)context;
	__asm__ volatile ("outb %0,%w1" : : "a"(value), "Nd"(port));
}

static int
gdc_command(struct boot98_beui_pc98_gdc *backend, uint8_t command)
{
	unsigned timeout;

	for (timeout = 100000U; timeout != 0; timeout--)
		if (!(backend->port_in8(backend->io_context, 0x60) & 0x02U))
			break;
	if (timeout == 0)
		return 0;
	backend->port_out8(backend->io_context, 0x62, command);
	return 1;
}

static void
clear_planes(struct boot98_beui_pc98_gdc *backend)
{
	unsigned plane;
	unsigned offset;

	for (plane = 0; plane < 4; plane++)
		for (offset = 0; offset < BOOT98_BEUI_GDC_PLANE_BYTES; offset++)
			backend->planes[plane][offset] = 0;
}

int
boot98_beui_pc98_gdc_clear_graphics(struct boot98_beui_pc98_gdc *backend)
{
	if (backend == NULL || backend->port_out8 == NULL ||
	    backend->planes[0] == NULL || backend->planes[1] == NULL ||
	    backend->planes[2] == NULL || backend->planes[3] == NULL)
		return 0;

	/* Match the real-mode loader's transition sequence.  In particular,
	 * disable GRCG/EGC interception before touching all four planar VRAM
	 * apertures; firmware and Cirrus may leave those controls non-default. */
	backend->port_out8(backend->io_context, 0x7cU, 0x00U);
	backend->port_out8(backend->io_context, 0x5fU, 0x00U);
	backend->port_out8(backend->io_context, 0x6aU, 0x07U);
	backend->port_out8(backend->io_context, 0x6aU, 0x20U);
	backend->port_out8(backend->io_context, 0x6aU, 0x04U);
	backend->port_out8(backend->io_context, 0x6aU, 0x06U);
	backend->port_out8(backend->io_context, 0x6aU, 0x01U);
	backend->port_out8(backend->io_context, 0x5fU, 0x01U);
	clear_planes(backend);
	return 1;
}

static uint8_t
rgb_to_gdc(uint32_t color)
{
	unsigned red = (color >> 16) & 0xffU;
	unsigned green = (color >> 8) & 0xffU;
	unsigned blue = color & 0xffU;
	unsigned luminance = (red + (green << 1) + blue) >> 2;

	/*
	 * Keep the integer-only StratoHAL RGBI conversion model, but use a
	 * half-range threshold for each component and a green-weighted
	 * luminance for the intensity plane.  The B/R/G bit order is the
	 * native PC-98 GDC plane order.
	 */
	return (uint8_t)((blue > 127U ? 1U : 0U) |
			 (red > 127U ? 2U : 0U) |
			 (green > 127U ? 4U : 0U) |
			 (luminance > 127U ? 8U : 0U));
}

static void
gdc_write_pixel(struct boot98_beui_pc98_gdc *backend, unsigned x,
		unsigned y, uint8_t color)
{
	unsigned offset = y * GDC_STRIDE + (x >> 3);
	uint8_t mask = (uint8_t)(0x80U >> (x & 7U));
	unsigned plane;

	for (plane = 0; plane < 4; plane++) {
		uint8_t old = backend->planes[plane][offset];

		backend->planes[plane][offset] = (uint8_t)(
			(old & (uint8_t)~mask) |
			(((color >> plane) & 1U) ? mask : 0U));
	}
}

static int
pattern_bit(uint64_t pattern, unsigned x, unsigned y)
{
	uint8_t row = (uint8_t)(pattern >> ((y & 7U) * 8U));

	return (row & (uint8_t)(0x80U >> (x & 7U))) != 0;
}

static int
gdc_enter(void *context, struct boot98_beui_display_info *info)
{
	struct boot98_beui_pc98_gdc *backend = context;

	if (backend == NULL || info == NULL || backend->display_reset == NULL ||
	    backend->port_in8 == NULL || backend->port_out8 == NULL ||
	    backend->planes[0] == NULL || backend->planes[1] == NULL ||
	    backend->planes[2] == NULL || backend->planes[3] == NULL ||
	    !backend->display_reset(backend->bios_context))
		return 0;
	/* Select 16-color GDC access, clear the visible planes, then hide text. */
	backend->port_out8(backend->io_context, 0x6a, 0x01);
	clear_planes(backend);
	if (!gdc_command(backend, 0x0c)) {
		(void)gdc_command(backend, 0x0d);
		return 0;
	}
	info->width = GDC_WIDTH;
	info->height = GDC_HEIGHT;
	info->bits_per_pixel = 4;
	info->stride = GDC_STRIDE;
	return 1;
}

static void
gdc_leave(void *context)
{
	struct boot98_beui_pc98_gdc *backend = context;

	if (backend->display_stop != NULL)
		(void)backend->display_stop(backend->bios_context);
	clear_planes(backend);
	(void)gdc_command(backend, 0x0d);
}

static int
gdc_fill(void *context, const struct boot98_beui_rect *rect, uint32_t color)
{
	struct boot98_beui_pc98_gdc *backend = context;
	uint8_t gdc_color = rgb_to_gdc(color);
	unsigned first_byte = rect->x >> 3;
	unsigned last_pixel = rect->x + rect->width - 1U;
	unsigned last_byte = last_pixel >> 3;
	unsigned y;
	unsigned byte;
	unsigned plane;

	for (y = rect->y; y < rect->y + rect->height; y++) {
		for (byte = first_byte; byte <= last_byte; byte++) {
			uint8_t mask = 0xffU;
			unsigned offset = y * GDC_STRIDE + byte;

			if (byte == first_byte)
				mask &= (uint8_t)(0xffU >> (rect->x & 7U));
			if (byte == last_byte)
				mask &= (uint8_t)(0xffU << (7U - (last_pixel & 7U)));
			for (plane = 0; plane < 4; plane++) {
				uint8_t old = backend->planes[plane][offset];

				backend->planes[plane][offset] =
					(uint8_t)((old & (uint8_t)~mask) |
					(((gdc_color >> plane) & 1U) ? mask : 0));
			}
		}
	}
	return 1;
}

static int
gdc_line(void *context, unsigned x0, unsigned y0, unsigned x1, unsigned y1,
	 uint32_t color)
{
	struct boot98_beui_pc98_gdc *backend = context;
	int x = (int)x0;
	int y = (int)y0;
	int target_x = (int)x1;
	int target_y = (int)y1;
	int delta_x = target_x >= x ? target_x - x : x - target_x;
	int step_x = x < target_x ? 1 : -1;
	int delta_y = target_y >= y ? y - target_y : target_y - y;
	int step_y = y < target_y ? 1 : -1;
	int error = delta_x + delta_y;
	uint8_t gdc_color = rgb_to_gdc(color);

	for (;;) {
		int twice_error;

		gdc_write_pixel(backend, (unsigned)x, (unsigned)y, gdc_color);
		if (x == target_x && y == target_y)
			break;
		twice_error = error * 2;
		if (twice_error >= delta_y) {
			error += delta_y;
			x += step_x;
		}
		if (twice_error <= delta_x) {
			error += delta_x;
			y += step_y;
		}
	}
	return 1;
}

static int
gdc_pattern_fill(void *context, const struct boot98_beui_rect *rect,
		 uint32_t color, uint64_t pattern)
{
	struct boot98_beui_pc98_gdc *backend = context;
	uint8_t gdc_color = rgb_to_gdc(color);
	unsigned y;

	for (y = rect->y; y < rect->y + rect->height; y++) {
		unsigned x;

		for (x = rect->x; x < rect->x + rect->width; x++)
			if (pattern_bit(pattern, x - rect->x, y - rect->y))
				gdc_write_pixel(backend, x, y, gdc_color);
	}
	return 1;
}

static int
gdc_draw_image_common(void *context, unsigned destination_x,
		      unsigned destination_y,
		      const struct boot98_beui_image *image, uint64_t pattern)
{
	struct boot98_beui_pc98_gdc *backend = context;
	unsigned y;

	for (y = 0; y < image->height; y++) {
		const uint8_t *row = image->pixels + (size_t)y * image->stride;
		unsigned x;

		for (x = 0; x < image->width; x++) {
			uint32_t rgb;

			if (!pattern_bit(pattern, x, y))
				continue;

			if (image->format == BOOT98_BEUI_IMAGE_INDEX8) {
				unsigned index = row[x];

				rgb = index < image->palette_size ?
					image->palette[index] : 0;
			} else {
				const uint8_t *pixel = row + (size_t)x * 3U;

				rgb = ((uint32_t)pixel[0] << 16) |
				      ((uint32_t)pixel[1] << 8) | pixel[2];
			}
			gdc_write_pixel(backend, destination_x + x,
					destination_y + y, rgb_to_gdc(rgb));
		}
	}
	return 1;
}

static int
gdc_draw_image(void *context, unsigned destination_x, unsigned destination_y,
	       const struct boot98_beui_image *image)
{
	return gdc_draw_image_common(context, destination_x, destination_y, image,
				     UINT64_MAX);
}

static int
gdc_draw_image_pattern(void *context, unsigned destination_x,
		       unsigned destination_y,
		       const struct boot98_beui_image *image, uint64_t pattern)
{
	return gdc_draw_image_common(context, destination_x, destination_y, image,
				     pattern);
}

static int
gdc_flush(void *context, const struct boot98_beui_rect *rectangles,
	  size_t rectangle_count)
{
	/* GDC VRAM is updated directly; there is no backing-image copy. */
	(void)context;
	(void)rectangles;
	(void)rectangle_count;
	return 1;
}

void
boot98_beui_pc98_gdc_default(struct boot98_beui_pc98_gdc *backend,
			     boot98_beui_display_reset_fn display_reset,
			     boot98_beui_display_reset_fn display_stop,
			     void *bios_context)
{
	memset(backend, 0, sizeof(*backend));
	backend->bios_context = bios_context;
	backend->display_reset = display_reset;
	backend->display_stop = display_stop;
	backend->port_in8 = native_in8;
	backend->port_out8 = native_out8;
	backend->planes[0] = (volatile uint8_t *)0x000a8000U;
	backend->planes[1] = (volatile uint8_t *)0x000b0000U;
	backend->planes[2] = (volatile uint8_t *)0x000b8000U;
	backend->planes[3] = (volatile uint8_t *)0x000e0000U;
}

int
boot98_beui_pc98_gdc_make_hal(struct boot98_beui_hal *hal,
			      struct boot98_beui_pc98_gdc *backend)
{
	if (hal == NULL || backend == NULL)
		return 0;
	memset(hal, 0, sizeof(*hal));
	hal->display.context = backend;
	hal->display.enter = gdc_enter;
	hal->display.leave = gdc_leave;
	hal->display.fill = gdc_fill;
	hal->display.line = gdc_line;
	hal->display.pattern_fill = gdc_pattern_fill;
	hal->display.draw_image = gdc_draw_image;
	hal->display.draw_image_pattern = gdc_draw_image_pattern;
	hal->display.flush = gdc_flush;
	return 1;
}
