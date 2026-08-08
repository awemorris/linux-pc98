/*
 * Boots Noct target adapters
 * Copyright (C) 2026 Awe Morris
 * SPDX-License-Identifier: Zlib
 */

#include "boot98-noct-target.h"
#include "boot98-noct-napi.h"

#include <noct/noct.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

struct target_term {
	const struct boot98_noct_services *services;
	unsigned row;
	unsigned column;
	uint8_t attribute;
	int open;
};

static struct target_term terminal;

static int directory_read(void *context, const char *path, size_t index,
			  char *name, size_t capacity, int *is_directory)
{
	const struct boot98_noct_services *services = context;
	struct boot98_noct_dirent entry;
	size_t length;
	int result;

	if (services == NULL || services->directory_read == NULL ||
	    name == NULL || capacity == 0 || is_directory == NULL ||
	    index > 0xffffffffU)
		return -1;
	result = services->directory_read(services->context, path,
					  (unsigned)index, &entry);
	if (result <= 0)
		return result;
	length = strnlen(entry.name, sizeof(entry.name));
	if (length >= capacity)
		return -1;
	memcpy(name, entry.name, length);
	name[length] = '\0';
	*is_directory = (entry.attributes & 0x10U) != 0;
	return 1;
}

static int term_open(void *context)
{
	struct target_term *term = context;

	if (term->services == NULL || term->services->screen_clear == NULL)
		return 0;
	if (!term->services->screen_clear(term->services->context))
		return 0;
	term->row = 0;
	term->column = 0;
	term->attribute = 0xe1U;
	term->open = 1;
	if (term->services->screen_show_cursor != NULL)
		(void)term->services->screen_show_cursor(
			term->services->context, 1);
	return 1;
}

static void term_close(void *context)
{
	struct target_term *term = context;

	if (term->services != NULL &&
	    term->services->screen_show_cursor != NULL)
		(void)term->services->screen_show_cursor(
			term->services->context, 1);
	term->open = 0;
}

static int term_is_tty(void *context)
{
	struct target_term *term = context;

	return term->services != NULL &&
		term->services->screen_put_utf8 != NULL &&
		term->services->keyboard_read != NULL;
}

static int term_size(void *context, unsigned *rows, unsigned *columns)
{
	(void)context;
	if (rows == NULL || columns == NULL)
		return 0;
	*rows = 25;
	*columns = 80;
	return 1;
}

static int term_resized(void *context)
{
	(void)context;
	return 0;
}

static int term_move_to(void *context, unsigned row, unsigned column)
{
	struct target_term *term = context;

	if (term->services == NULL || term->services->screen_set_cursor == NULL ||
	    row == 0 || row > 25 || column == 0 || column > 80)
		return 0;
	term->row = row - 1U;
	term->column = column - 1U;
	return term->services->screen_set_cursor(term->services->context,
						 term->row, term->column);
}

static int term_write(void *context, const char *utf8, size_t length)
{
	struct target_term *term = context;
	int cells;

	if (term->services == NULL || term->services->screen_put_utf8 == NULL ||
	    utf8 == NULL || length > 0xffffffffU)
		return 0;
	cells = term->services->screen_put_utf8(term->services->context,
		term->row, term->column, utf8, (unsigned)length,
		term->attribute);
	if (cells < 0)
		return 0;
	term->column += (unsigned)cells;
	if (term->column > 79U)
		term->column = 79U;
	return 1;
}

static int term_clear(void *context)
{
	struct target_term *term = context;

	if (term->services == NULL || term->services->screen_clear == NULL ||
	    !term->services->screen_clear(term->services->context))
		return 0;
	term->row = 0;
	term->column = 0;
	return 1;
}

static int term_clear_to_eol(void *context)
{
	struct target_term *term = context;

	return term->services != NULL &&
		term->services->screen_clear_to_eol != NULL &&
		term->services->screen_clear_to_eol(term->services->context,
						   term->row, term->column);
}

static int term_set_style(void *context, const struct NoctTermStyle *style)
{
	struct target_term *term = context;
	uint8_t attribute = 0xe1U;

	if (style == NULL)
		return 0;
	if (style->foreground >= 0)
		attribute = (uint8_t)(1U |
			((unsigned)style->foreground & 7U) << 5);
	if (style->reverse)
		attribute |= 0x04U;
	if (style->underline)
		attribute |= 0x08U;
	term->attribute = attribute;
	return 1;
}

static int term_show_cursor(void *context, int visible)
{
	struct target_term *term = context;

	return term->services != NULL &&
		term->services->screen_show_cursor != NULL &&
		term->services->screen_show_cursor(term->services->context,
						  visible);
}

static int term_flush(void *context)
{
	struct target_term *term = context;

	if (term->services != NULL &&
	    term->services->screen_set_cursor != NULL)
		(void)term->services->screen_set_cursor(
			term->services->context, term->row, term->column);
	return 1;
}

static int translate_key(int bios_key)
{
	int key = boot98_key_normalize_bios_ax((uint16_t)bios_key);

	switch (key) {
	case BOOT98_KEY_BACKSPACE:
		return 0x7f;
	case BOOT98_KEY_ENTER:
	case BOOT98_KEY_ESCAPE:
		return key;
	case BOOT98_KEY_UP:
		return NOCT_TERM_KEY_UP;
	case BOOT98_KEY_DOWN:
		return NOCT_TERM_KEY_DOWN;
	case BOOT98_KEY_RIGHT:
		return NOCT_TERM_KEY_RIGHT;
	case BOOT98_KEY_LEFT:
		return NOCT_TERM_KEY_LEFT;
	case BOOT98_KEY_HOME:
		return NOCT_TERM_KEY_HOME;
	case BOOT98_KEY_END:
		return NOCT_TERM_KEY_END;
	case BOOT98_KEY_PAGE_UP:
		return NOCT_TERM_KEY_PGUP;
	case BOOT98_KEY_PAGE_DOWN:
		return NOCT_TERM_KEY_PGDN;
	case BOOT98_KEY_INSERT:
		return NOCT_TERM_KEY_INSERT;
	case BOOT98_KEY_DELETE:
		return NOCT_TERM_KEY_DELETE;
	default:
		break;
	}
	if (key >= BOOT98_KEY_F1 && key <= BOOT98_KEY_F10)
		return NOCT_TERM_KEY_F1 + key - BOOT98_KEY_F1;
	if (key == 0)
		return NOCT_TERM_MOD_CTRL | 0x20;
	if (key > 0 && key <= 0x1a)
		return NOCT_TERM_MOD_CTRL | (key + 0x60);
	if (key > 0x1a && key < 0x20)
		return NOCT_TERM_MOD_CTRL | (key + 0x40);
	return key;
}

/*
 * POSIX terminals encode M-x as ESC followed by x and Noct folds that byte
 * sequence into one META event. The PC-98 BIOS reports the same input as two
 * key events, so the target adapter performs the equivalent folding here.
 *
 * The available firmware clock has one-second resolution. Waiting for two
 * changes guarantees at least one full second for a human to type the suffix,
 * even when ESC arrived immediately before a second boundary. A bare ESC is
 * therefore delayed by one to two seconds, but never blocks indefinitely.
 */
static int read_meta_suffix(struct target_term *term)
{
	int previous;
	unsigned changes = 0;

	if (term->services->keyboard_poll == NULL)
		return -1;
	if (term->services->clock_second == NULL)
		return term->services->keyboard_poll(term->services->context) >= 0 ?
			term->services->keyboard_read(term->services->context) : -1;
	previous = term->services->clock_second(term->services->context);
	if (previous < 0 || previous > 59)
		return -1;
	for (;;) {
		int second;

		if (term->services->keyboard_poll(term->services->context) >= 0)
			return term->services->keyboard_read(
				term->services->context);
		second = term->services->clock_second(term->services->context);
		if (second < 0 || second > 59)
			return -1;
		if (second != previous) {
			previous = second;
			if (++changes == 2U)
				return -1;
		}
	}
}

static int term_read_key(void *context, int timeout_ms)
{
	struct target_term *term = context;
	int key = -1;
	int translated;

	if (term->services == NULL || term->services->keyboard_read == NULL)
		return -1;
	if (term->services->keyboard_poll != NULL &&
	    term->services->keyboard_poll(term->services->context) >= 0)
		key = term->services->keyboard_read(term->services->context);
	else if (timeout_ms != 0)
		key = term->services->keyboard_read(term->services->context);
	if (key < 0)
		return -1;
	translated = translate_key(key);
	if (translated == 0x1b) {
		key = read_meta_suffix(term);
		if (key >= 0)
			return NOCT_TERM_MOD_META | translate_key(key);
	}
	return translated;
}

static int term_pending_input(void *context)
{
	struct target_term *term = context;

	return term->services != NULL &&
		term->services->keyboard_poll != NULL &&
		term->services->keyboard_poll(term->services->context) >= 0;
}

int boot98_noct_target_register(NoctEnv *env,
				const struct boot98_noct_services *services)
{
	static const struct NoctDirectoryBackend directory = {
		.read = directory_read,
	};
	static const struct NoctTermBackend term = {
		.open = term_open,
		.close = term_close,
		.is_tty = term_is_tty,
		.size = term_size,
		.resized = term_resized,
		.move_to = term_move_to,
		.write = term_write,
		.clear = term_clear,
		.clear_to_eol = term_clear_to_eol,
		.set_style = term_set_style,
		.show_cursor = term_show_cursor,
		.flush = term_flush,
		.read_key = term_read_key,
		.pending_input = term_pending_input,
	};

	memset(&terminal, 0, sizeof(terminal));
	terminal.services = services;
	terminal.attribute = 0xe1U;
	noct_set_directory_backend(&directory, (void *)services);
	return noct_register_api_file(env) &&
		noct_register_api_term_backend(env, &term, &terminal);
}

void boot98_noct_target_cleanup(void)
{
	noct_set_directory_backend(NULL, NULL);
	memset(&terminal, 0, sizeof(terminal));
}
