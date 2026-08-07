/*
 * PC-9800 Bootloader
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "boot98-console.h"

#define TERMINAL_FIRST_ROW 18U

static volatile uint16_t *const text_vram =
	(volatile uint16_t *)0x000a0000;
static volatile uint8_t *const attribute_vram =
	(volatile uint8_t *)0x000a2000;
static enum boot98_console_mode console_mode;
static unsigned cursor_row;
static unsigned cursor_column;
static int cursor_visible;

static uint8_t port_in8(uint16_t port)
{
	uint8_t value;

	asm volatile("inb %w1, %0" : "=a" (value) : "Nd" (port));
	return value;
}

static void port_out8(uint16_t port, uint8_t value)
{
	asm volatile("outb %0, %w1" : : "a" (value), "Nd" (port));
}

/* Wait for room in the uPD7220 FIFO before each command or parameter byte. */
static int gdc_write(uint16_t port, uint8_t value)
{
	unsigned timeout;

	for (timeout = 100000; timeout; timeout--)
		if (!(port_in8(0x60) & 0x02))
			break;
	if (!timeout)
		return 0;
	port_out8(port, value);
	return 1;
}

static void write_cell(unsigned row, unsigned column, uint16_t code,
		       uint8_t attribute)
{
	unsigned offset = row * BOOT98_CONSOLE_COLUMNS + column;

	text_vram[offset] = code;
	attribute_vram[offset * 2] = attribute;
}

void boot98_console_clear_row(unsigned row)
{
	if (row >= BOOT98_CONSOLE_ROWS)
		return;
	for (unsigned column = 0; column < BOOT98_CONSOLE_COLUMNS; column++)
		write_cell(row, column, ' ', BOOT98_CONSOLE_NORMAL_ATTRIBUTE);
}

void boot98_console_clear(void)
{
	for (unsigned row = 0; row < BOOT98_CONSOLE_ROWS; row++)
		boot98_console_clear_row(row);
}

void boot98_console_reset(void)
{
	boot98_console_clear();
	console_mode = BOOT98_CONSOLE_FIXED_MENU;
	cursor_row = 0;
	cursor_column = 0;
	cursor_visible = 1;
}

static void scroll(void)
{
	for (unsigned row = 0; row + 1 < BOOT98_CONSOLE_ROWS; row++)
		for (unsigned column = 0; column < BOOT98_CONSOLE_COLUMNS;
		     column++) {
			unsigned destination = row * BOOT98_CONSOLE_COLUMNS + column;
			unsigned source = destination + BOOT98_CONSOLE_COLUMNS;

			text_vram[destination] = text_vram[source];
			attribute_vram[destination * 2] =
				attribute_vram[source * 2];
		}
	boot98_console_clear_row(BOOT98_CONSOLE_ROWS - 1);
	cursor_row = BOOT98_CONSOLE_ROWS - 1;
}

static void newline(void)
{
	cursor_column = 0;
	if (++cursor_row < BOOT98_CONSOLE_ROWS)
		return;
	if (console_mode == BOOT98_CONSOLE_TERMINAL)
		scroll();
	else
		cursor_row = BOOT98_CONSOLE_ROWS - 1;
}

static void put_single_cell(uint16_t code)
{
	if (cursor_column >= BOOT98_CONSOLE_COLUMNS)
		newline();
	write_cell(cursor_row, cursor_column, code,
		   BOOT98_CONSOLE_NORMAL_ATTRIBUTE);
	cursor_column++;
}

void boot98_console_putc(uint8_t byte)
{
	if (byte == '\n') {
		newline();
		return;
	}
	if (byte == '\r') {
		cursor_column = 0;
		return;
	}
	if (byte == '\b') {
		if (cursor_column) {
			cursor_column--;
			write_cell(cursor_row, cursor_column, ' ',
				   BOOT98_CONSOLE_NORMAL_ATTRIBUTE);
		}
		return;
	}
	put_single_cell(byte);
}

static int is_sjis_lead(uint8_t byte)
{
	return (byte >= 0x81 && byte <= 0x9f) ||
	       (byte >= 0xe0 && byte <= 0xef);
}

static int is_sjis_trail(uint8_t byte)
{
	return byte >= 0x40 && byte <= 0xfc && byte != 0x7f;
}

/* Convert one Shift-JIS double-byte character to the PC-98 text VRAM code. */
static uint16_t sjis_to_pc98(uint8_t lead, uint8_t trail)
{
	uint8_t row = lead <= 0x9f ? (lead - 0x71) * 2 + 1
					 : (lead - 0xb1) * 2 + 1;

	if (trail > 0x7f)
		trail--;
	if (trail >= 0x9e) {
		trail -= 0x7d;
		row++;
	} else {
		trail -= 0x1f;
	}
	return ((uint16_t)trail << 8) | (uint8_t)(row - 0x20);
}

void boot98_console_puts_sjis(const uint8_t *string)
{
	while (*string) {
		uint8_t lead = *string++;

		if (!is_sjis_lead(lead)) {
			if (lead >= 0x80 && !(lead >= 0xa1 && lead <= 0xdf))
				lead = '?';
			boot98_console_putc(lead);
			continue;
		}
		if (!*string || !is_sjis_trail(*string)) {
			boot98_console_putc('?');
			continue;
		}
		if (cursor_column + 1 >= BOOT98_CONSOLE_COLUMNS)
			newline();
		uint16_t code = sjis_to_pc98(lead, *string++);
		write_cell(cursor_row, cursor_column++, code,
			   BOOT98_CONSOLE_NORMAL_ATTRIBUTE);
		write_cell(cursor_row, cursor_column++, code | 0x8000,
			   BOOT98_CONSOLE_NORMAL_ATTRIBUTE);
	}
}

/* Positional writes deliberately leave the logical cursor after the string. */
void boot98_console_write_at(unsigned row, unsigned column,
			     const uint8_t *string)
{
	if (row >= BOOT98_CONSOLE_ROWS ||
	    column >= BOOT98_CONSOLE_COLUMNS)
		return;
	cursor_row = row;
	cursor_column = column;
	boot98_console_puts_sjis(string);
}

void boot98_console_clear_to_eol(void)
{
	for (unsigned column = cursor_column;
	     column < BOOT98_CONSOLE_COLUMNS; column++)
		write_cell(cursor_row, column, ' ',
			   BOOT98_CONSOLE_NORMAL_ATTRIBUTE);
}

/*
 * Write one row without scrolling.  The return value is the number of text
 * cells changed; a double-byte character is never split at the right edge.
 */
int boot98_console_put_sjis_at(unsigned row, unsigned column,
			       const uint8_t *string, uint8_t attribute)
{
	unsigned start = column;

	if (row >= BOOT98_CONSOLE_ROWS ||
	    column >= BOOT98_CONSOLE_COLUMNS || string == 0)
		return -1;
	while (*string != 0 && column < BOOT98_CONSOLE_COLUMNS) {
		uint8_t lead = *string++;

		if (!is_sjis_lead(lead)) {
			if (lead >= 0x80 && !(lead >= 0xa1 && lead <= 0xdf))
				lead = '?';
			write_cell(row, column++, lead, attribute);
			continue;
		}
		if (*string == 0 || !is_sjis_trail(*string)) {
			write_cell(row, column++, '?', attribute);
			continue;
		}
		if (column + 1U >= BOOT98_CONSOLE_COLUMNS)
			break;
		uint16_t code = sjis_to_pc98(lead, *string++);
		write_cell(row, column++, code, attribute);
		write_cell(row, column++, code | 0x8000U, attribute);
	}
	cursor_row = row;
	cursor_column = column < BOOT98_CONSOLE_COLUMNS ? column :
		BOOT98_CONSOLE_COLUMNS - 1U;
	return (int)(column - start);
}

int boot98_console_set_cursor(unsigned row, unsigned column)
{
	if (row >= BOOT98_CONSOLE_ROWS ||
	    column >= BOOT98_CONSOLE_COLUMNS)
		return 0;
	cursor_row = row;
	cursor_column = column;
	boot98_console_update_cursor();
	return 1;
}

void boot98_console_show_cursor(int visible)
{
	cursor_visible = visible != 0;
	boot98_console_update_cursor();
}

void boot98_console_save_state(struct boot98_console_state *state)
{
	if (state == 0)
		return;
	state->mode = console_mode;
	state->row = cursor_row;
	state->column = cursor_column;
	state->cursor_visible = cursor_visible;
}

void boot98_console_restore_terminal(const struct boot98_console_state *state)
{
	unsigned output_row = cursor_row;
	unsigned output_column = cursor_column;

	console_mode = BOOT98_CONSOLE_TERMINAL;
	if (output_row >= BOOT98_CONSOLE_ROWS) {
		output_row = TERMINAL_FIRST_ROW;
		output_column = 0;
	}
	if (state != 0 && state->mode == BOOT98_CONSOLE_TERMINAL &&
	    state->row < BOOT98_CONSOLE_ROWS &&
	    state->column < BOOT98_CONSOLE_COLUMNS &&
	    state->row > output_row) {
		cursor_row = state->row;
		cursor_column = state->column;
	} else {
		cursor_row = output_row;
		cursor_column = output_column;
	}
	if (cursor_column != 0)
		newline();
	cursor_visible = 1;
	boot98_console_update_cursor();
}

void boot98_console_set_mode(enum boot98_console_mode mode)
{
	console_mode = mode;
	if (mode == BOOT98_CONSOLE_TERMINAL) {
		cursor_row = TERMINAL_FIRST_ROW;
		cursor_column = 0;
	}
}

/* Program CSRFORM as well as CSRW so firmware cannot leave the cursor hidden. */
void boot98_console_update_cursor(void)
{
	unsigned address = cursor_row * BOOT98_CONSOLE_COLUMNS + cursor_column;

	if (!gdc_write(0x62, 0x4b) ||
	    !gdc_write(0x60, cursor_visible ? 0x8f : 0x0f) ||
	    !gdc_write(0x60, 0x20) ||
	    !gdc_write(0x60, 0x7b) ||
	    !gdc_write(0x62, 0x49) ||
	    !gdc_write(0x60, (uint8_t)address))
		return;
	gdc_write(0x60, (uint8_t)(address >> 8));
}
