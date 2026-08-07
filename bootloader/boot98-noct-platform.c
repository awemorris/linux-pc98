/*
 * PC-98 Bootstrap Environment Noct target adapter
 * Copyright (C) 2026 Awe Morris
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "boot98-console.h"
#include "boot98-noct.h"
#include "boot98-noct-platform.h"

#include <stddef.h>
#include <stdint.h>

#define SCRIPT_ARENA_BASE 0x00100000U
#define SCRIPT_ARENA_LIMIT 0x00f00000U
#define SCRIPT_ARENA_GUARD 0x00010000U

static const char embedded_source[] =
	"func main() { "
	"Console.write(\"Noct M5 float: \" + (123.0f / 321.0f)); "
	"Console.write(\" double: \" + (123.0lf / 321.0lf)); "
	"Console.write(\" sqrt: \" + Math.sqrt(9.0f)); "
	"Console.write(\" sin: \" + Math.sin(0.5f)); "
	"Console.write(\" cos: \" + Math.cos(0.5f)); "
	"Console.write(\" tan: \" + Math.tan(0.5f)); }";

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
	struct boot98_noct_result result;
	size_t arena_size;
	unsigned iteration;

	if (repeat_count == 0 || repeat_count > 100U)
		return 0;
	enable_high_memory();
	arena_size = script_arena_size();
	if (arena_size < 2U * 1024U * 1024U) {
		console_string("Noct: insufficient script arena\n");
		boot98_console_update_cursor();
		return 0;
	}
	options.arena = (void *)SCRIPT_ARENA_BASE;
	options.arena_size = arena_size;
	options.fail_after = BOOT98_NOCT_NO_FAILURE;
	options.write = console_writer;
	options.write_context = NULL;
	for (iteration = 0; iteration < repeat_count; iteration++) {
		if (!boot98_noct_run("<embedded>", embedded_source, &options,
				     &result)) {
			console_string("Noct M5 failed: ");
			console_string(boot98_noct_status_string(result.status));
			console_string("\n");
			boot98_console_update_cursor();
			return 0;
		}
		if (result.current_after_reset != 0) {
			console_string("Noct M5 cleanup failed\n");
			boot98_console_update_cursor();
			return 0;
		}
		console_string("\n");
	}
	console_string("Noct M5 PASS: runs=");
	console_decimal(repeat_count);
	console_string(" peak=");
	console_decimal(result.heap_peak);
	console_string(" bytes\n");
	boot98_console_update_cursor();
	return 1;
}
