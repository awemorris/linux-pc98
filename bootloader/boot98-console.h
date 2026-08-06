/*
 * PC-9800 Bootloader
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef BOOT98_CONSOLE_H
#define BOOT98_CONSOLE_H

#include <stdint.h>

enum boot98_console_mode {
	BOOT98_CONSOLE_FIXED_MENU,
	BOOT98_CONSOLE_TERMINAL,
};

void boot98_console_reset(void);
void boot98_console_set_mode(enum boot98_console_mode mode);
void boot98_console_putc(uint8_t byte);
void boot98_console_puts_sjis(const uint8_t *string);
void boot98_console_write_at(unsigned row, unsigned column,
			     const uint8_t *string);
void boot98_console_clear_row(unsigned row);
void boot98_console_clear_to_eol(void);
void boot98_console_update_cursor(void);

#endif
