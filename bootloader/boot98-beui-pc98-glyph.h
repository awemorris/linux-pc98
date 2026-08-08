/*
 * Boots PC-98 CGROM glyph backend
 * Copyright (C) 2026 Awe Morris
 * SPDX-License-Identifier: Zlib
 */

#ifndef BOOT98_BEUI_PC98_GLYPH_H
#define BOOT98_BEUI_PC98_GLYPH_H

#include "boot98-beui.h"

#include <stdint.h>

struct boot98_beui_pc98_glyph {
	void *io_context;
	uint8_t (*port_in8)(void *context, uint16_t port);
	void (*port_out8)(void *context, uint16_t port, uint8_t value);
	volatile uint8_t *cg_window;
	struct boot98_beui_display_hal *display;
	struct {
		uint16_t jis;
		uint8_t valid;
		uint8_t font[32];
	} cache[64];
	unsigned cache_next;
};

void boot98_beui_pc98_glyph_default(
	struct boot98_beui_pc98_glyph *backend,
	struct boot98_beui_display_hal *display);
int boot98_beui_pc98_glyph_make_hal(struct boot98_beui_glyph_hal *hal,
	struct boot98_beui_pc98_glyph *backend);
uint16_t boot98_beui_pc98_unicode_to_jis(uint32_t codepoint);

#endif
