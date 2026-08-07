/*
 * PC-98 Bootstrap Environment Noct target adapter
 * Copyright (C) 2026 Awe Morris
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "boot98-console.h"
#include "boot98-fs.h"
#include "boot98-noct.h"
#include "boot98-noct-napi.h"
#include "boot98-noct-m6-script.h"
#include "boot98-noct-platform.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define SCRIPT_ARENA_BASE 0x00100000U
#define SCRIPT_ARENA_LIMIT 0x00f00000U
#define SCRIPT_ARENA_GUARD 0x00010000U
#define SCRIPT_HEAP_MIN (2U * 1024U * 1024U)

static const char embedded_source[] = BOOT98_NOCT_M6_SOURCE;

struct target_context {
	struct boot98_filesystem *filesystem;
	boot98_noct_key_fn key_read;
	boot98_noct_key_fn key_poll;
	void *key_context;
};

static void
console_string(const char *string)
{
	while (*string != '\0')
		boot98_console_putc((uint8_t)*string++);
}

static void
console_decimal(size_t value)
{
	char digits[11];
	unsigned count = 0;

	if (value == 0) {
		boot98_console_putc('0');
		return;
	}
	while (value != 0 && count < sizeof(digits)) {
		digits[count++] = (char)('0' + value % 10U);
		value /= 10U;
	}
	while (count != 0)
		boot98_console_putc((uint8_t)digits[--count]);
}

static size_t
console_writer(void *context, const char *bytes, size_t length)
{
	size_t index;

	(void)context;
	for (index = 0; index < length; index++)
		boot98_console_putc((uint8_t)bytes[index]);
	return length;
}

static int
target_screen_clear(void *context)
{
	(void)context;
	boot98_console_clear();
	return 1;
}

static int
target_screen_clear_row(void *context, unsigned row)
{
	(void)context;
	if (row >= BOOT98_CONSOLE_ROWS)
		return 0;
	boot98_console_clear_row(row);
	return 1;
}

static int
target_screen_put(void *context, unsigned row, unsigned column,
		  const char *text, uint8_t attribute)
{
	(void)context;
	return boot98_console_put_sjis_at(row, column,
					  (const uint8_t *)text, attribute);
}

static int
target_screen_set_cursor(void *context, unsigned row, unsigned column)
{
	(void)context;
	return boot98_console_set_cursor(row, column);
}

static int
target_screen_show_cursor(void *context, int visible)
{
	(void)context;
	boot98_console_show_cursor(visible);
	return 1;
}

static int
target_keyboard_read(void *context)
{
	struct target_context *target = context;

	return target->key_read != NULL ? target->key_read(target->key_context) :
		-1;
}

static int
target_keyboard_poll(void *context)
{
	struct target_context *target = context;

	return target->key_poll != NULL ? target->key_poll(target->key_context) :
		-1;
}

static int
target_file_size(void *context, const char *path, uint32_t *size)
{
	struct target_context *target = context;
	struct boot98_file file;

	if (target->filesystem == NULL || size == NULL ||
	    !boot98_fs_open(target->filesystem, path, &file) ||
	    file.size > UINT32_MAX)
		return 0;
	*size = (uint32_t)file.size;
	return 1;
}

static int
target_file_read(void *context, const char *path, uint32_t offset,
		 void *buffer, uint32_t length)
{
	struct target_context *target = context;
	struct boot98_file file;

	return target->filesystem != NULL &&
	       boot98_fs_open(target->filesystem, path, &file) &&
	       boot98_file_read(&file, offset, buffer, length);
}

static int
target_directory_read(void *context, const char *path, unsigned index,
		      struct boot98_noct_dirent *entry)
{
	struct target_context *target = context;
	struct boot98_dirent filesystem_entry;
	size_t length;

	if (target->filesystem == NULL || entry == NULL || path == NULL ||
	    (path[0] != '\0' && !(path[0] == '/' && path[1] == '\0')))
		return -1;
	if (!boot98_fs_readdir(target->filesystem, path, index,
				 &filesystem_entry))
		return 0;
	length = strnlen(filesystem_entry.name, sizeof(filesystem_entry.name));
	if (length >= sizeof(entry->name))
		return -1;
	memcpy(entry->name, filesystem_entry.name, length);
	entry->name[length] = '\0';
	entry->size = filesystem_entry.size;
	entry->attributes = filesystem_entry.attributes;
	return 1;
}

static void
make_services(struct boot98_noct_services *services,
	      struct target_context *context)
{
	services->context = context;
	services->screen_clear = target_screen_clear;
	services->screen_clear_row = target_screen_clear_row;
	services->screen_put = target_screen_put;
	services->screen_set_cursor = target_screen_set_cursor;
	services->screen_show_cursor = target_screen_show_cursor;
	services->keyboard_poll = target_keyboard_poll;
	services->keyboard_read = target_keyboard_read;
	services->file_size = target_file_size;
	services->file_read = target_file_read;
	services->directory_read = target_directory_read;
}

size_t
boot98_console_write_bytes(const char *bytes, size_t length)
{
	return console_writer(NULL, bytes, length);
}

__attribute__((noreturn)) void
boot98_libc_panic(const char *message)
{
	console_string("Noct fatal: ");
	console_string(message != NULL ? message : "unknown");
	console_string("\n");
	boot98_console_update_cursor();
	for (;;)
		__asm__ volatile ("cli; hlt");
}

static void
enable_high_memory(void)
{
	__asm__ volatile(
		"xorb %%al,%%al; outb %%al,$0xf2; movb $2,%%al; outb "
		"%%al,$0xf6; movw $0x439,%%dx; inb %%dx,%%al; andb $0xfb,%%al; "
		"outb %%al,%%dx; xorb %%al,%%al; outb %%al,$0xf8; movw "
		"$0x43b,%%dx; movb $4,%%al; outb %%al,%%dx" ::
			: "eax", "edx");
}

static uint8_t
read_low_byte(uint32_t address)
{
	uint8_t value;

	__asm__ volatile ("movb (%1),%0" : "=q"(value) : "r"(address));
	return value;
}

static size_t
script_arena_size(void)
{
	uint32_t extended = (uint32_t)read_low_byte(0x401U) << 17;
	uint32_t end = SCRIPT_ARENA_BASE + extended;

	if (end < SCRIPT_ARENA_BASE || end > SCRIPT_ARENA_LIMIT)
		end = SCRIPT_ARENA_LIMIT;
	if (end <= SCRIPT_ARENA_BASE + SCRIPT_ARENA_GUARD)
		return 0;
	return end - SCRIPT_ARENA_BASE - SCRIPT_ARENA_GUARD;
}

int
boot98_noct_run_embedded(unsigned repeat_count)
{
	struct boot98_noct_options options;
	struct boot98_noct_services services;
	struct target_context target = { 0 };
	struct boot98_console_state console_state;
	struct boot98_noct_result result;
	size_t arena_size;
	unsigned iteration;

	if (repeat_count == 0 || repeat_count > 100U)
		return 0;
	enable_high_memory();
	make_services(&services, &target);
	arena_size = script_arena_size();
	if (arena_size < 2U * 1024U * 1024U) {
		console_string("Noct: insufficient script arena\n");
		boot98_console_update_cursor();
		return 0;
	}
	options.arena = (void *)SCRIPT_ARENA_BASE;
	options.arena_size = arena_size;
	options.fail_after = BOOT98_NOCT_NO_FAILURE;
	options.jit_enable = 1;
	options.jit_threshold = 1;
	options.write = console_writer;
	options.write_context = NULL;
	options.observe_jit_code = NULL;
	options.jit_context = NULL;
	options.services = &services;
	boot98_console_save_state(&console_state);
	for (iteration = 0; iteration < repeat_count; iteration++) {
		if (!boot98_noct_run("<embedded>", embedded_source, &options,
				     &result)) {
			console_string("Noct M6 failed: ");
			console_string(boot98_noct_status_string(result.status));
			console_string("\n");
			boot98_console_update_cursor();
			boot98_console_restore_terminal(&console_state);
			return 0;
		}
		if (result.current_after_reset != 0 ||
		    result.jit_code_size != BOOT98_NOCT_JIT_CODE_MAX ||
		    !result.jit_region_released) {
			console_string("Noct M6 cleanup/JIT check failed\n");
			boot98_console_update_cursor();
			boot98_console_restore_terminal(&console_state);
			return 0;
		}
		console_string("\n");
	}
	console_string("Noct M6 JIT PASS: runs=");
	console_decimal(repeat_count);
	console_string(" peak=");
	console_decimal(result.heap_peak);
	console_string(" bytes\n");
	boot98_console_restore_terminal(&console_state);
	return 1;
}

int
boot98_noct_run_file(struct boot98_filesystem *filesystem, const char *path,
		     int argc, char *const argv[], boot98_noct_key_fn key_read,
		     boot98_noct_key_fn key_poll, void *key_context)
{
	struct boot98_noct_options options;
	struct boot98_noct_services services;
	struct target_context target;
	struct boot98_console_state console_state;
	struct boot98_noct_result result;
	struct boot98_file file;
	size_t arena_size;
	size_t source_area;
	char *source;
	int ok;

	if (filesystem == NULL || path == NULL || path[0] == '\0')
		return 0;
	if (!boot98_fs_open(filesystem, path, &file)) {
		console_string("Noct: file not found: ");
		console_string(path);
		console_string("\n");
		boot98_console_update_cursor();
		return 0;
	}
	if (file.size > BOOT98_NOCT_SOURCE_MAX) {
		console_string("Noct: source exceeds 256 KiB: ");
		console_string(path);
		console_string("\n");
		boot98_console_update_cursor();
		return 0;
	}

	enable_high_memory();
	target.filesystem = filesystem;
	target.key_read = key_read;
	target.key_poll = key_poll;
	target.key_context = key_context;
	make_services(&services, &target);
	arena_size = script_arena_size();
	source_area = ((size_t)file.size + 1U + 15U) & ~(size_t)15U;
	if (arena_size < source_area + SCRIPT_HEAP_MIN) {
		console_string("Noct: insufficient script arena\n");
		boot98_console_update_cursor();
		return 0;
	}
	source = (char *)(SCRIPT_ARENA_BASE + arena_size - source_area);
	if (file.size != 0 &&
	    !boot98_file_read(&file, 0, source, (uint32_t)file.size)) {
		console_string("Noct: cannot read source: ");
		console_string(path);
		console_string("\n");
		boot98_console_update_cursor();
		return 0;
	}
	source[(size_t)file.size] = '\0';

	options.arena = (void *)SCRIPT_ARENA_BASE;
	options.arena_size = arena_size - source_area;
	options.fail_after = BOOT98_NOCT_NO_FAILURE;
	options.jit_enable = 1;
	options.jit_threshold = 1;
	options.write = console_writer;
	options.write_context = NULL;
	options.observe_jit_code = NULL;
	options.jit_context = NULL;
	options.services = &services;
	boot98_console_save_state(&console_state);
	ok = boot98_noct_run_args(path, source, argc, argv, &options, &result);
	if (!ok) {
		console_string("Noct: ");
		console_string(boot98_noct_status_string(result.status));
		console_string("\n");
	} else if (result.script_status != 0) {
		console_string("Noct: script returned nonzero status\n");
		ok = 0;
	}
	boot98_console_restore_terminal(&console_state);
	return ok;
}
