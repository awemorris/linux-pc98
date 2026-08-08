/*
 * Boots graphical environment lifecycle and hardware abstraction
 * Copyright (C) 2026 Awe Morris
 * SPDX-License-Identifier: Zlib
 */

#include "boot98-beui.h"

#include <string.h>

struct boot98_beui_state {
	const struct boot98_beui_hal *hal;
	struct boot98_beui_display_info display;
	int display_open;
	int pointer_open;
	int audio_open;
};

static struct boot98_beui_state state;

int
boot98_beui_bind(const struct boot98_beui_hal *hal)
{
	if (state.display_open || state.pointer_open || state.audio_open)
		return 0;
	state.hal = hal;
	return 1;
}

int
boot98_beui_init(void)
{
	if (state.display_open)
		return 1;
	if (state.hal == NULL || state.hal->display.enter == NULL ||
	    state.hal->display.leave == NULL)
		return 0;
	memset(&state.display, 0, sizeof(state.display));
	if (!state.hal->display.enter(state.hal->display.context,
				      &state.display))
		return 0;
	state.display_open = 1;
	if (state.display.width == 0 || state.display.height == 0)
		goto fail;
	if (state.hal->pointer.start != NULL) {
		if (!state.hal->pointer.start(state.hal->pointer.context,
					      &state.display))
			goto fail;
		state.pointer_open = 1;
	}
	return 1;

fail:
	boot98_beui_close();
	return 0;
}

void
boot98_beui_close(void)
{
	if (state.hal == NULL)
		return;
	if (state.audio_open && state.hal->audio.stop != NULL)
		state.hal->audio.stop(state.hal->audio.context);
	state.audio_open = 0;
	if (state.pointer_open && state.hal->pointer.stop != NULL)
		state.hal->pointer.stop(state.hal->pointer.context);
	state.pointer_open = 0;
	if (state.display_open && state.hal->display.leave != NULL)
		state.hal->display.leave(state.hal->display.context);
	state.display_open = 0;
	memset(&state.display, 0, sizeof(state.display));
}

void
boot98_beui_cleanup(void)
{
	boot98_beui_close();
	memset(&state, 0, sizeof(state));
}

int
boot98_beui_is_open(void)
{
	return state.display_open;
}

int
boot98_beui_get_display_info(struct boot98_beui_display_info *info)
{
	if (!state.display_open || info == NULL)
		return 0;
	*info = state.display;
	return 1;
}

int
boot98_beui_fill(const struct boot98_beui_rect *rect, uint32_t color)
{
	if (!state.display_open || rect == NULL || rect->width == 0 ||
	    rect->height == 0 || rect->x >= state.display.width ||
	    rect->y >= state.display.height ||
	    rect->width > state.display.width - rect->x ||
	    rect->height > state.display.height - rect->y ||
	    state.hal->display.fill == NULL)
		return 0;
	return state.hal->display.fill(state.hal->display.context, rect, color);
}

int
boot98_beui_line(unsigned x0, unsigned y0, unsigned x1, unsigned y1,
		 uint32_t color)
{
	if (!state.display_open || x0 >= state.display.width ||
	    x1 >= state.display.width || y0 >= state.display.height ||
	    y1 >= state.display.height || state.hal->display.line == NULL)
		return 0;
	return state.hal->display.line(state.hal->display.context, x0, y0, x1,
				       y1, color);
}

int
boot98_beui_pattern_fill(const struct boot98_beui_rect *rect, uint32_t color,
			 uint64_t pattern)
{
	if (!state.display_open || rect == NULL || rect->width == 0 ||
	    rect->height == 0 || rect->x >= state.display.width ||
	    rect->y >= state.display.height ||
	    rect->width > state.display.width - rect->x ||
	    rect->height > state.display.height - rect->y ||
	    state.hal->display.pattern_fill == NULL)
		return 0;
	return state.hal->display.pattern_fill(state.hal->display.context, rect,
					       color, pattern);
}

int
boot98_beui_draw_image(unsigned x, unsigned y,
		       const struct boot98_beui_image *image)
{
	size_t minimum_stride;

	if (!state.display_open || image == NULL || image->pixels == NULL ||
	    image->width == 0 || image->height == 0 ||
	    (image->format != BOOT98_BEUI_IMAGE_INDEX8 &&
	     image->format != BOOT98_BEUI_IMAGE_RGB24) ||
	    (image->format == BOOT98_BEUI_IMAGE_INDEX8 &&
	     (image->palette_size == 0 || image->palette_size > 256)) ||
	    x >= state.display.width || y >= state.display.height ||
	    image->width > state.display.width - x ||
	    image->height > state.display.height - y ||
	    state.hal->display.draw_image == NULL)
		return 0;
	minimum_stride = image->format == BOOT98_BEUI_IMAGE_RGB24 ?
		(size_t)image->width * 3U : image->width;
	if (image->stride < minimum_stride)
		return 0;
	return state.hal->display.draw_image(state.hal->display.context, x, y,
					     image);
}

int
boot98_beui_draw_image_pattern(unsigned x, unsigned y,
			       const struct boot98_beui_image *image,
			       uint64_t pattern)
{
	size_t minimum_stride;

	if (!state.display_open || image == NULL || image->pixels == NULL ||
	    image->width == 0 || image->height == 0 ||
	    (image->format != BOOT98_BEUI_IMAGE_INDEX8 &&
	     image->format != BOOT98_BEUI_IMAGE_RGB24) ||
	    (image->format == BOOT98_BEUI_IMAGE_INDEX8 &&
	     (image->palette_size == 0 || image->palette_size > 256)) ||
	    x >= state.display.width || y >= state.display.height ||
	    image->width > state.display.width - x ||
	    image->height > state.display.height - y ||
	    state.hal->display.draw_image_pattern == NULL)
		return 0;
	minimum_stride = image->format == BOOT98_BEUI_IMAGE_RGB24 ?
		(size_t)image->width * 3U : image->width;
	if (image->stride < minimum_stride)
		return 0;
	return state.hal->display.draw_image_pattern(
		state.hal->display.context, x, y, image, pattern);
}

static uint32_t
decode_utf8(const char **cursor)
{
	const unsigned char *text = (const unsigned char *)*cursor;
	uint32_t codepoint;
	unsigned length;
	unsigned index;

	if (text[0] < 0x80U) {
		(*cursor)++;
		return text[0];
	}
	if ((text[0] & 0xe0U) == 0xc0U) {
		codepoint = text[0] & 0x1fU;
		length = 2;
	} else if ((text[0] & 0xf0U) == 0xe0U) {
		codepoint = text[0] & 0x0fU;
		length = 3;
	} else if ((text[0] & 0xf8U) == 0xf0U) {
		codepoint = text[0] & 0x07U;
		length = 4;
	} else {
		(*cursor)++;
		return 0xfffdU;
	}
	for (index = 1; index < length; index++) {
		if ((text[index] & 0xc0U) != 0x80U) {
			(*cursor)++;
			return 0xfffdU;
		}
		codepoint = (codepoint << 6) | (text[index] & 0x3fU);
	}
	if ((length == 2 && codepoint < 0x80U) ||
	    (length == 3 && codepoint < 0x800U) ||
	    (length == 4 && codepoint < 0x10000U) || codepoint > 0x10ffffU ||
	    (codepoint >= 0xd800U && codepoint <= 0xdfffU)) {
		(*cursor)++;
		return 0xfffdU;
	}
	*cursor += length;
	return codepoint;
}

int
boot98_beui_measure_text(const char *text, unsigned *width, unsigned *height)
{
	const char *cursor = text;
	unsigned line_width = 0;
	unsigned maximum_width = 0;
	unsigned total_height = 16;

	if (!state.display_open || text == NULL || width == NULL ||
	    height == NULL || state.hal->glyph.measure == NULL)
		return 0;
	while (*cursor != '\0') {
		uint32_t codepoint = decode_utf8(&cursor);
		unsigned glyph_width;
		unsigned glyph_height;

		if (codepoint == '\r')
			continue;
		if (codepoint == '\n') {
			if (line_width > maximum_width)
				maximum_width = line_width;
			line_width = 0;
			if (total_height > (unsigned)-1 - 16U)
				return 0;
			total_height += 16U;
			continue;
		}
		if (!state.hal->glyph.measure(state.hal->glyph.context, codepoint,
			&glyph_width, &glyph_height) || glyph_height > 16U ||
		    line_width > (unsigned)-1 - glyph_width)
			return 0;
		line_width += glyph_width;
	}
	if (line_width > maximum_width)
		maximum_width = line_width;
	*width = maximum_width;
	*height = total_height;
	return 1;
}

int
boot98_beui_draw_text(const char *text, unsigned x, unsigned y,
	uint32_t foreground, uint32_t background)
{
	const char *cursor = text;
	unsigned origin_x = x;
	unsigned width;
	unsigned height;

	if (!boot98_beui_measure_text(text, &width, &height) ||
	    x > state.display.width || y > state.display.height ||
	    width > state.display.width - x ||
	    height > state.display.height - y || state.hal->glyph.draw == NULL)
		return 0;
	while (*cursor != '\0') {
		uint32_t codepoint = decode_utf8(&cursor);
		unsigned glyph_width;
		unsigned glyph_height;

		if (codepoint == '\r')
			continue;
		if (codepoint == '\n') {
			x = origin_x;
			y += 16U;
			continue;
		}
		if (!state.hal->glyph.measure(state.hal->glyph.context, codepoint,
			&glyph_width, &glyph_height) ||
		    !state.hal->glyph.draw(state.hal->glyph.context, x, y,
			codepoint, foreground, background))
			return 0;
		x += glyph_width;
	}
	return 1;
}

int
boot98_beui_poll(void)
{
	struct boot98_beui_pointer_event event;

	if (!state.display_open)
		return 0;
	if (state.pointer_open && state.hal->pointer.poll != NULL) {
		memset(&event, 0, sizeof(event));
		if (state.hal->pointer.poll(state.hal->pointer.context, &event) < 0)
			return 0;
	}
	if (state.audio_open && state.hal->audio.poll != NULL &&
	    !state.hal->audio.poll(state.hal->audio.context))
		return 0;
	return 1;
}

int
boot98_beui_flush(void)
{
	if (!state.display_open)
		return 0;
	if (state.hal->display.flush == NULL)
		return 1;
	return state.hal->display.flush(state.hal->display.context, NULL, 0);
}
