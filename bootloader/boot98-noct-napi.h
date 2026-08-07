/*
 * PC-98 Bootstrap Environment Noct native APIs
 * Copyright (C) 2026 Awe Morris
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef BOOT98_NOCT_NAPI_H
#define BOOT98_NOCT_NAPI_H

#include <stddef.h>
#include <stdint.h>

typedef struct rt_env NoctEnv;

#define BOOT98_NOCT_DIRECTORY_MAX 256U
#define BOOT98_NOCT_PATH_MAX 256U
#define BOOT98_NOCT_SOURCE_MAX (256U * 1024U)

enum boot98_key_code {
	BOOT98_KEY_ESCAPE = 0x1b,
	BOOT98_KEY_BACKSPACE = 0x08,
	BOOT98_KEY_ENTER = 0x0d,
	BOOT98_KEY_PAGE_UP = 0x136,
	BOOT98_KEY_PAGE_DOWN = 0x137,
	BOOT98_KEY_INSERT = 0x138,
	BOOT98_KEY_DELETE = 0x139,
	BOOT98_KEY_UP = 0x13a,
	BOOT98_KEY_LEFT = 0x13b,
	BOOT98_KEY_RIGHT = 0x13c,
	BOOT98_KEY_DOWN = 0x13d,
	BOOT98_KEY_HOME = 0x13e,
	BOOT98_KEY_END = 0x13f,
	BOOT98_KEY_F1 = 0x162,
	BOOT98_KEY_F2 = 0x163,
	BOOT98_KEY_F3 = 0x164,
	BOOT98_KEY_F4 = 0x165,
	BOOT98_KEY_F5 = 0x166,
	BOOT98_KEY_F6 = 0x167,
	BOOT98_KEY_F7 = 0x168,
	BOOT98_KEY_F8 = 0x169,
	BOOT98_KEY_F9 = 0x16a,
	BOOT98_KEY_F10 = 0x16b,
};

struct boot98_noct_dirent {
	char name[BOOT98_NOCT_PATH_MAX];
	uint64_t size;
	uint8_t attributes;
};

/*
 * The core NAPI only knows this injectable interface.  The boot target maps it
 * to the GDC, BIOS keyboard gateway, and the selected filesystem.  Host tests
 * supply deterministic in-memory implementations.
 */
struct boot98_noct_services {
	void *context;
	int (*screen_clear)(void *context);
	int (*screen_clear_row)(void *context, unsigned row);
	int (*screen_put)(void *context, unsigned row, unsigned column,
			  const char *text, uint8_t attribute);
	int (*screen_set_cursor)(void *context, unsigned row, unsigned column);
	int (*screen_show_cursor)(void *context, int visible);
	int (*keyboard_poll)(void *context);
	int (*keyboard_read)(void *context);
	int (*file_size)(void *context, const char *path, uint32_t *size);
	int (*file_read)(void *context, const char *path, uint32_t offset,
			 void *buffer, uint32_t length);
	/* Return 1 for an entry, 0 at end, and -1 for an invalid path/I/O. */
	int (*directory_read)(void *context, const char *path, unsigned index,
			      struct boot98_noct_dirent *entry);
};

struct boot98_noct_options;

/* Convert the PC-98 BIOS AX pair to the stable BE key namespace. */
int boot98_key_normalize_bios_ax(uint16_t bios_ax);

int boot98_noct_napi_register(NoctEnv *env,
			      const struct boot98_noct_options *options);
void boot98_noct_napi_cleanup(void);

#endif
