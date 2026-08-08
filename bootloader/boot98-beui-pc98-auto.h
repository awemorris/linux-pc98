/*
 * Boots BeUI PC-98 display selection
 * Copyright (C) 2026 Awe Morris
 * SPDX-License-Identifier: Zlib
 */

#ifndef BOOT98_BEUI_PC98_AUTO_H
#define BOOT98_BEUI_PC98_AUTO_H

#include "boot98-beui-pc98-cirrus.h"
#include "boot98-beui-pc98-glyph.h"
#include "boot98-beui-pc98.h"

struct boot98_beui_pc98_auto {
	struct boot98_beui_pc98_cirrus cirrus;
	struct boot98_beui_pc98_gdc gdc;
	struct boot98_beui_pc98_glyph glyph;
	struct boot98_beui_hal cirrus_hal;
	struct boot98_beui_hal gdc_hal;
	struct boot98_beui_display_hal *active;
};

void boot98_beui_pc98_auto_default(
	struct boot98_beui_pc98_auto *backend,
	boot98_beui_display_reset_fn display_reset,
	boot98_beui_display_reset_fn display_stop, void *bios_context);
int boot98_beui_pc98_auto_make_hal(struct boot98_beui_hal *hal,
	struct boot98_beui_pc98_auto *backend);

#endif
