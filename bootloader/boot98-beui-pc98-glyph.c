/*
 * Boots PC-98 CGROM glyph backend
 * Copyright (C) 2026 Awe Morris
 * SPDX-License-Identifier: Zlib
 *
 * CGROM selection and read sequencing is adapted from StratoHAL 98glyph.c,
 * commit 76e909577bdf4629f11e473539b446a948fef830.  In particular, preserve
 * its VSYNC exclusion, 0x68 mode switch, CG-window byte layout, and symbol
 * bank handling; these details matter on physical PC-98 hardware.
 */

#include "boot98-beui-pc98-glyph.h"

#include <string.h>

/* Already linked by Noct's Term/File API; share it to avoid another table. */
extern const uint16_t noct_jisx0208_to_ucs[7896];

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

static void
wait_vsync(struct boot98_beui_pc98_glyph *backend)
{
	while (backend->port_in8(backend->io_context, 0x60) & 0x20U)
		;
	while (!(backend->port_in8(backend->io_context, 0x60) & 0x20U))
		;
}

uint16_t
boot98_beui_pc98_unicode_to_jis(uint32_t codepoint)
{
	size_t index;
	uint16_t value;

	if (codepoint > 0xffffU)
		return 0;
	value = (uint16_t)codepoint;
	if (value < 0x80U)
		return (uint16_t)(0x2000U | value);
	if (value >= 0xff61U && value <= 0xff9fU)
		return (uint16_t)(0x20a1U + (value - 0xff61U));

	for (index = 0; index < 7896U; index++) {
		if (noct_jisx0208_to_ucs[index] == value) {
			unsigned row = (unsigned)(index / 94U) + 0x21U;
			unsigned cell = (unsigned)(index % 94U) + 0x21U;

			return (uint16_t)((row << 8) | cell);
		}
	}
	return 0;
}

static unsigned
glyph_width(uint16_t jis)
{
	return (jis >> 8) == 0x20U ? 8U : 16U;
}

static int
read_font(struct boot98_beui_pc98_glyph *backend, uint16_t jis,
	  uint8_t font[32])
{
	uint8_t row = (uint8_t)(jis >> 8);
	uint8_t cell = (uint8_t)jis;
	int special = (row >= 0x29U && row <= 0x2fU) ||
		(row >= 0x76U && row <= 0x7fU);
	unsigned index;

	if (backend->port_in8 == NULL || backend->port_out8 == NULL ||
	    backend->cg_window == NULL)
		return 0;
	if (!special) {
		for (index = 0; index < 64U; index++) {
			if (backend->cache[index].valid &&
			    backend->cache[index].jis == jis) {
				memcpy(font, backend->cache[index].font, 32);
				return 1;
			}
		}
	}
	memset(font, 0, 32);
	wait_vsync(backend);
	backend->port_out8(backend->io_context, 0x68, 0x0b);

	if (row == 0x20U) {
		backend->port_out8(backend->io_context, 0xa1, 0x00);
		backend->port_out8(backend->io_context, 0xa3, cell);
		backend->port_out8(backend->io_context, 0xa5, 0x00);
		for (index = 0; index < 16U; index++)
			font[index] = backend->cg_window[index * 2U + 1U];
	} else if (!special) {
		backend->port_out8(backend->io_context, 0xa1, cell);
		backend->port_out8(backend->io_context, 0xa3,
			(uint8_t)(row - 0x20U));
		backend->port_out8(backend->io_context, 0xa5, 0x00);
		for (index = 0; index < 32U; index++)
			font[index] = backend->cg_window[index];
	} else {
		backend->port_out8(backend->io_context, 0xa1, cell);
		backend->port_out8(backend->io_context, 0xa3,
			(uint8_t)(row - 0x20U));
		backend->port_out8(backend->io_context, 0xa5, 0x20);
		for (index = 0; index < 16U; index++)
			font[index * 2U] =
				backend->cg_window[index * 2U + 1U];
		backend->port_out8(backend->io_context, 0xa5, 0x00);
		for (index = 0; index < 16U; index++)
			font[index * 2U + 1U] =
				backend->cg_window[index * 2U + 1U];
	}

	backend->port_out8(backend->io_context, 0x68, 0x0a);
	if (!special) {
		index = backend->cache_next++ % 64U;
		backend->cache[index].jis = jis;
		backend->cache[index].valid = 1;
		memcpy(backend->cache[index].font, font, 32);
	}
	return 1;
}

static int
glyph_measure(void *context, uint32_t codepoint, unsigned *width,
	      unsigned *height)
{
	uint16_t jis;

	(void)context;
	if (width == NULL || height == NULL)
		return 0;
	jis = boot98_beui_pc98_unicode_to_jis(codepoint);
	if (jis == 0)
		jis = boot98_beui_pc98_unicode_to_jis('?');
	*width = glyph_width(jis);
	*height = 16;
	return 1;
}

static uint64_t
font_pattern(const uint8_t font[32], unsigned bytes_per_row,
	     unsigned byte_index, unsigned first_row)
{
	uint64_t pattern = 0;
	unsigned row;

	for (row = 0; row < 8U; row++)
		pattern |= (uint64_t)font[(first_row + row) * bytes_per_row +
			byte_index] << (row * 8U);
	return pattern;
}

static int
glyph_draw(void *context, unsigned x, unsigned y, uint32_t codepoint,
	   uint32_t foreground, uint32_t background)
{
	struct boot98_beui_pc98_glyph *backend = context;
	struct boot98_beui_rect rectangle;
	uint8_t font[32];
	uint16_t jis = boot98_beui_pc98_unicode_to_jis(codepoint);
	unsigned width;
	unsigned column;
	unsigned band;

	if (jis == 0)
		jis = boot98_beui_pc98_unicode_to_jis('?');
	width = glyph_width(jis);
	if (backend == NULL || backend->display == NULL ||
	    backend->display->fill == NULL ||
	    backend->display->pattern_fill == NULL ||
	    !read_font(backend, jis, font))
		return 0;

	rectangle.x = x;
	rectangle.y = y;
	rectangle.width = width;
	rectangle.height = 16;
	if (!backend->display->fill(backend->display->context, &rectangle,
		background))
		return 0;

	for (band = 0; band < 2U; band++) {
		for (column = 0; column < width / 8U; column++) {
			rectangle.x = x + column * 8U;
			rectangle.y = y + band * 8U;
			rectangle.width = 8;
			rectangle.height = 8;
			if (!backend->display->pattern_fill(
				backend->display->context, &rectangle, foreground,
				font_pattern(font, width / 8U, column, band * 8U)))
				return 0;
		}
	}
	return 1;
}

void
boot98_beui_pc98_glyph_default(struct boot98_beui_pc98_glyph *backend,
	struct boot98_beui_display_hal *display)
{
	memset(backend, 0, sizeof(*backend));
	backend->port_in8 = native_in8;
	backend->port_out8 = native_out8;
	backend->cg_window = (volatile uint8_t *)0x000a4000U;
	backend->display = display;
}

int
boot98_beui_pc98_glyph_make_hal(struct boot98_beui_glyph_hal *hal,
	struct boot98_beui_pc98_glyph *backend)
{
	if (hal == NULL || backend == NULL || backend->display == NULL ||
	    backend->port_in8 == NULL || backend->port_out8 == NULL ||
	    backend->cg_window == NULL)
		return 0;
	memset(hal, 0, sizeof(*hal));
	hal->context = backend;
	hal->measure = glyph_measure;
	hal->draw = glyph_draw;
	return 1;
}
