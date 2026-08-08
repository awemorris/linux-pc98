/*
 * Boots BeUI NEC PC-9821 Core-Graph / Cirrus GD5440 backend
 * Copyright (C) 2026 Awe Morris
 * Copyright (C) 1996-2024 Keiichi Tabata
 * SPDX-License-Identifier: Zlib
 *
 * The register sequence is adapted from StratoHAL 98disp_cirrus.c at
 * commit 76e909577bdf4629f11e473539b446a948fef830.  This Boots version is
 * deliberately limited to the verified Core-Graph path and 640x480x8bpp.
 */

#ifndef BOOT98_BEUI_PC98_CIRRUS_H
#define BOOT98_BEUI_PC98_CIRRUS_H

#include "boot98-beui.h"

#include <stdint.h>

#define BOOT98_BEUI_CIRRUS_WIDTH 640U
#define BOOT98_BEUI_CIRRUS_HEIGHT 480U
#define BOOT98_BEUI_CIRRUS_STRIDE 640U
#define BOOT98_BEUI_CIRRUS_VISIBLE_BYTES \
	(BOOT98_BEUI_CIRRUS_STRIDE * BOOT98_BEUI_CIRRUS_HEIGHT)

struct boot98_beui_pc98_cirrus {
	void *io_context;
	uint8_t (*port_in8)(void *context, uint16_t port);
	void (*port_out8)(void *context, uint16_t port, uint8_t value);
	volatile uint8_t *framebuffer;
	uint8_t saved_sleep;
	uint8_t saved_window;
	uint8_t saved_linear;
	uint8_t saved_relay;
	uint8_t active;
};

void boot98_beui_pc98_cirrus_default(
	struct boot98_beui_pc98_cirrus *backend);
int boot98_beui_pc98_cirrus_make_hal(
	struct boot98_beui_hal *hal,
	struct boot98_beui_pc98_cirrus *backend);

#endif
