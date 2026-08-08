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

#ifndef BOOT98_BEUI_PC98_H
#define BOOT98_BEUI_PC98_H

#include "boot98-beui.h"

#include <stdint.h>

#define BOOT98_BEUI_GDC_PLANE_BYTES (640U * 400U / 8U)

typedef int (*boot98_beui_display_reset_fn)(void *context);

struct boot98_beui_pc98_gdc {
	void *bios_context;
	boot98_beui_display_reset_fn display_reset;
	boot98_beui_display_reset_fn display_stop;
	void *io_context;
	uint8_t (*port_in8)(void *context, uint16_t port);
	void (*port_out8)(void *context, uint16_t port, uint8_t value);
	volatile uint8_t *planes[4];
};

void boot98_beui_pc98_gdc_default(
	struct boot98_beui_pc98_gdc *backend,
	boot98_beui_display_reset_fn display_reset,
	boot98_beui_display_reset_fn display_stop, void *bios_context);
int boot98_beui_pc98_gdc_make_hal(struct boot98_beui_hal *hal,
				   struct boot98_beui_pc98_gdc *backend);
int boot98_beui_pc98_gdc_clear_graphics(
	struct boot98_beui_pc98_gdc *backend);

#endif
