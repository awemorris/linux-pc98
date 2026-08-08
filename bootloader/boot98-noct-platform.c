/*
 * PC-98 Bootstrap Environment Noct target adapter
 * Copyright (C) 2026 Awe Morris
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "boot98-console.h"
#include "boot98-env.h"
#include "boot98-fs.h"
#include "boot98-noct.h"
#include "boot98-noct-memory.h"
#include "boot98-noct-napi.h"
#include "boot98-noct-m6-script.h"
#include "boot98-noct-platform.h"

#include <noct/noct.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define SCRIPT_HEAP_MIN (2U * 1024U * 1024U)

static const char embedded_source[] = BOOT98_NOCT_M6_SOURCE;

struct target_context {
	struct boot98_filesystem *filesystem;
	boot98_noct_key_fn key_read;
	boot98_noct_key_fn key_poll;
	boot98_noct_clock_fn clock_second;
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

static enum boot98_noct_repl_input_result
repl_read_line(void *context, int continuation, char *line, size_t capacity)
{
	struct target_context *target = context;
	size_t length = 0;

	if (target == NULL || target->key_read == NULL || line == NULL ||
	    capacity < 2U)
		return BOOT98_NOCT_REPL_INPUT_ERROR;
	console_string(continuation ? ". " : "> ");
	boot98_console_show_cursor(1);
	for (;;) {
		int key = target->key_read(target->key_context);

		if (key < 0)
			return BOOT98_NOCT_REPL_INPUT_ERROR;
		if (key > 0xff)
			continue;
		if (key == 0x03) {
			console_string("^C\n");
			line[0] = '\0';
			boot98_console_update_cursor();
			return BOOT98_NOCT_REPL_INPUT_EXIT;
		}
		if (key == '\r' || key == '\n') {
			boot98_console_putc('\n');
			line[length] = '\0';
			boot98_console_update_cursor();
			return BOOT98_NOCT_REPL_INPUT_LINE;
		}
		if (key == '\b' || key == 0x7f) {
			if (length != 0) {
				length--;
				boot98_console_putc('\b');
				boot98_console_update_cursor();
			}
			continue;
		}
		if (key >= 32 && key < 127 && length + 1U < capacity) {
			line[length++] = (char)key;
			boot98_console_putc((uint8_t)key);
			boot98_console_update_cursor();
		}
	}
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
target_screen_put_utf8(void *context, unsigned row, unsigned column,
		       const char *text, unsigned length, uint8_t attribute)
{
	(void)context;
	return boot98_console_put_utf8_at(row, column, text, length, attribute);
}

static int
target_screen_clear_to_eol(void *context, unsigned row, unsigned column)
{
	(void)context;
	return boot98_console_clear_to_eol_at(row, column);
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
target_clock_second(void *context)
{
	struct target_context *target = context;

	return target->clock_second != NULL ?
		target->clock_second(target->key_context) : -1;
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

	if (target->filesystem == NULL || entry == NULL || path == NULL)
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
	services->screen_put_utf8 = target_screen_put_utf8;
	services->screen_clear_to_eol = target_screen_clear_to_eol;
	services->screen_set_cursor = target_screen_set_cursor;
	services->screen_show_cursor = target_screen_show_cursor;
	services->keyboard_poll = target_keyboard_poll;
	services->keyboard_read = target_keyboard_read;
	services->clock_second = target_clock_second;
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

static uint16_t
read_low_word(uint32_t address)
{
	uint16_t value;

	__asm__ volatile ("movw (%1),%0" : "=r"(value) : "r"(address));
	return value;
}

static int
select_memory(struct boot98_noct_memory_profile *profile)
{
	uint32_t low_extended = (uint32_t)read_low_byte(0x401U) << 17;
	uint32_t high_mib = read_low_word(0x594U);

	return boot98_noct_select_memory(low_extended, high_mib, profile);
}

int
boot98_noct_run_embedded(unsigned repeat_count)
{
	struct boot98_noct_options options;
	struct boot98_noct_services services;
	struct target_context target = { 0 };
	struct boot98_console_state console_state;
	struct boot98_noct_result result;
	struct boot98_noct_memory_profile memory;
	unsigned iteration;

	if (repeat_count == 0 || repeat_count > 100U)
		return 0;
	enable_high_memory();
	make_services(&services, &target);
	if (!select_memory(&memory)) {
		console_string("Noct: insufficient script arena\n");
		boot98_console_update_cursor();
		return 0;
	}
	options.arena = (void *)memory.arena_base;
	options.arena_size = memory.arena_size;
	options.fail_after = BOOT98_NOCT_NO_FAILURE;
	options.jit_enable = 1;
	options.jit_threshold = 1;
	options.write = console_writer;
	options.write_context = NULL;
	options.observe_jit_code = NULL;
	options.jit_context = NULL;
	options.services = &services;
	options.filesystem = NULL;
	options.environment = NULL;
	options.memory = &memory;
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
		    result.jit_code_size != memory.jit_code_size ||
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
boot98_noct_run_file(struct boot98_filesystem *filesystem,
		     struct boot98_environment *environment, const char *path,
		     int argc, char *const argv[], boot98_noct_key_fn key_read,
		     boot98_noct_key_fn key_poll,
		     boot98_noct_clock_fn clock_second, void *key_context)
{
	struct boot98_noct_options options;
	struct boot98_noct_services services;
	struct target_context target;
	struct boot98_console_state console_state;
	struct boot98_noct_result result;
	struct boot98_file file;
	struct boot98_noct_memory_profile memory;
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
	enable_high_memory();
	target.filesystem = filesystem;
	target.key_read = key_read;
	target.key_poll = key_poll;
	target.clock_second = clock_second;
	target.key_context = key_context;
	make_services(&services, &target);
	if (!select_memory(&memory)) {
		console_string("Noct: insufficient script arena\n");
		boot98_console_update_cursor();
		return 0;
	}
	if (file.size > memory.source_max) {
		console_string("Noct: source exceeds memory profile limit: ");
		console_string(path);
		console_string("\n");
		boot98_console_update_cursor();
		return 0;
	}
	source_area = ((size_t)file.size + 1U + 15U) & ~(size_t)15U;
	if (memory.arena_size < source_area + SCRIPT_HEAP_MIN) {
		console_string("Noct: insufficient script arena\n");
		boot98_console_update_cursor();
		return 0;
	}
	source = (char *)(memory.arena_base + memory.arena_size - source_area);
	if (file.size != 0 &&
	    !boot98_file_read(&file, 0, source, (uint32_t)file.size)) {
		console_string("Noct: cannot read source: ");
		console_string(path);
		console_string("\n");
		boot98_console_update_cursor();
		return 0;
	}
	source[(size_t)file.size] = '\0';

	options.arena = (void *)memory.arena_base;
	options.arena_size = memory.arena_size - source_area;
	options.fail_after = BOOT98_NOCT_NO_FAILURE;
	options.jit_enable = 1;
	options.jit_threshold = 1;
	options.write = console_writer;
	options.write_context = NULL;
	options.observe_jit_code = NULL;
	options.jit_context = NULL;
	options.services = &services;
	options.filesystem = filesystem;
	options.environment = environment;
	options.memory = &memory;
	boot98_console_save_state(&console_state);
	if (file.size >= sizeof(NOCT_BYTECODE_HEADER) - 1U &&
	    memcmp(source, NOCT_BYTECODE_HEADER,
		   sizeof(NOCT_BYTECODE_HEADER) - 1U) == 0)
		ok = boot98_noct_run_bytecode_args(path, (uint8_t *)source,
						 (uint32_t)file.size, argc, argv,
						 &options, &result);
	else
		ok = boot98_noct_run_args(path, source, argc, argv, &options,
					  &result);
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

int
boot98_noct_run_repl(struct boot98_filesystem *filesystem,
		     struct boot98_environment *environment,
		     boot98_noct_key_fn key_read, boot98_noct_key_fn key_poll,
		     boot98_noct_clock_fn clock_second, void *key_context)
{
	struct boot98_noct_options options;
	struct boot98_noct_services services;
	struct target_context target;
	struct boot98_console_state console_state;
	struct boot98_noct_result result;
	struct boot98_noct_memory_profile memory;
	int ok;

	if (key_read == NULL)
		return 0;
	enable_high_memory();
	target.filesystem = filesystem;
	target.key_read = key_read;
	target.key_poll = key_poll;
	target.clock_second = clock_second;
	target.key_context = key_context;
	make_services(&services, &target);
	if (!select_memory(&memory)) {
		console_string("Noct: insufficient script arena\n");
		boot98_console_update_cursor();
		return 0;
	}

	options.arena = (void *)memory.arena_base;
	options.arena_size = memory.arena_size;
	options.fail_after = BOOT98_NOCT_NO_FAILURE;
	options.jit_enable = 1;
	options.jit_threshold = 1;
	options.write = console_writer;
	options.write_context = NULL;
	options.observe_jit_code = NULL;
	options.jit_context = NULL;
	options.services = &services;
	options.filesystem = filesystem;
	options.environment = environment;
	options.memory = &memory;
	boot98_console_save_state(&console_state);
	ok = boot98_noct_repl(&options, repl_read_line, &target, &result);
	if (!ok) {
		console_string("Noct: ");
		console_string(boot98_noct_status_string(result.status));
		console_string("\n");
	}
	boot98_console_restore_terminal(&console_state);
	return ok;
}
