// SPDX-License-Identifier: GPL-2.0
/*
 * NEC PC-9800 GDC text console.
 *
 * Copyright (C) 2026 Awe Morris
 *
 * The character/attribute plane layout and visible attribute value come from
 * the last official Linux PC-9800 boot console.  The consw implementation is
 * newly written for the Linux 7.1 console API.
 */

#include <linux/console.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/kd.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/vt_buffer.h>
#include <linux/vt_kern.h>

#include <asm/io.h>
#include <asm/vga.h>

#define PC98_TEXT_PHYS		0x000a0000
#define PC98_TEXT_MAP_SIZE	0x00004000
#define PC98_ATTR_OFFSET	0x00002000
#define PC98_COLUMNS		80
#define PC98_LINES		25
#define PC98_NORMAL_ATTR	0x00e1
#define PC98_GDC_STATUS		0x0060
#define PC98_GDC_PARAMETER	0x0060
#define PC98_GDC_COMMAND	0x0062
#define PC98_GDC_FIFO_FULL	0x02
#define PC98_GDC_CSRW		0x49
#define PC98_GDC_CSRFORM		0x4b
#define PC98_GDC_TIMEOUT	100000
#define PC98_IO_WAIT		0x005f
#define PC98_CURSOR_RASTER	0x0f
#define PC98_CURSOR_START	0x00
#define PC98_CURSOR_END		0x7b

static u16 *pc98_chars;
static u16 *pc98_attrs;
static struct vc_data *pc98_foreground;

static bool pc98_gdc_write(u16 port, u8 value)
{
	int timeout;

	for (timeout = PC98_GDC_TIMEOUT; timeout > 0; timeout--)
		if (!(inb(PC98_GDC_STATUS) & PC98_GDC_FIFO_FULL))
			break;
	if (!timeout)
		return false;

	outb(value, port);
	/* Port 0x5f provides the fixed GDC recovery delay used on PC-98. */
	outb(0, PC98_IO_WAIT);
	outb(0, PC98_IO_WAIT);
	return true;
}

static inline u16 *pc98_char_at(unsigned int x, unsigned int y)
{
	return pc98_chars + y * PC98_COLUMNS + x;
}

static inline u16 *pc98_attr_at(unsigned int x, unsigned int y)
{
	return pc98_attrs + y * PC98_COLUMNS + x;
}

static const char *pc98con_startup(void)
{
	pc98_chars = (u16 *)VGA_MAP_MEM(PC98_TEXT_PHYS, PC98_TEXT_MAP_SIZE);
	pc98_attrs = pc98_chars + PC98_ATTR_OFFSET / sizeof(*pc98_attrs);

	return "PC-98-TEXT";
}

static void pc98con_init(struct vc_data *vc, bool init)
{
	vc->vc_can_do_color = true;
	vc->vc_complement_mask = 0x0400;
	vc->vc_display_fg = &pc98_foreground;

	if (init) {
		vc->vc_cols = PC98_COLUMNS;
		vc->vc_rows = PC98_LINES;
	} else {
		vc_resize(vc, PC98_COLUMNS, PC98_LINES);
	}

	if (!pc98_foreground)
		pc98_foreground = vc;
}

static void pc98con_deinit(struct vc_data *vc)
{
	if (pc98_foreground == vc)
		pc98_foreground = NULL;
}

static u16 pc98con_attribute(u16 cell)
{
	return (cell >> 8) & 0xff;
}

static void pc98con_putc(struct vc_data *vc, u16 cell,
			 unsigned int y, unsigned int x)
{
	scr_writew(cell & 0xff, pc98_char_at(x, y));
	scr_writew(pc98con_attribute(cell), pc98_attr_at(x, y));
}

static void pc98con_putcs(struct vc_data *vc, const u16 *source,
			  unsigned int count, unsigned int y,
			  unsigned int x)
{
	u16 *chars = pc98_char_at(x, y);
	u16 *attrs = pc98_attr_at(x, y);

	while (count--) {
		u16 cell = scr_readw(source++);

		scr_writew(cell & 0xff, chars++);
		scr_writew(pc98con_attribute(cell), attrs++);
	}
}

static void pc98con_clear(struct vc_data *vc, unsigned int y,
			  unsigned int x, unsigned int count)
{
	u16 *chars = pc98_char_at(x, y);
	u16 *attrs = pc98_attr_at(x, y);

	while (count--) {
		scr_writew(' ', chars++);
		scr_writew(PC98_NORMAL_ATTR, attrs++);
	}
}

static bool pc98con_scroll(struct vc_data *vc, unsigned int top,
			   unsigned int bottom, enum con_scroll direction,
			   unsigned int lines)
{
	unsigned int cells;

	if (!lines || top >= bottom)
		return false;
	if (lines > bottom - top)
		lines = bottom - top;

	cells = (bottom - top - lines) * PC98_COLUMNS;
	if (direction == SM_UP) {
		scr_memmovew(pc98_char_at(0, top), pc98_char_at(0, top + lines),
			     cells * sizeof(u16));
		scr_memmovew(pc98_attr_at(0, top), pc98_attr_at(0, top + lines),
			     cells * sizeof(u16));
		pc98con_clear(vc, bottom - lines, 0, lines * PC98_COLUMNS);
	} else {
		scr_memmovew(pc98_char_at(0, top + lines), pc98_char_at(0, top),
			     cells * sizeof(u16));
		scr_memmovew(pc98_attr_at(0, top + lines), pc98_attr_at(0, top),
			     cells * sizeof(u16));
		pc98con_clear(vc, top, 0, lines * PC98_COLUMNS);
	}

	return false;
}

static void pc98con_cursor(struct vc_data *vc, bool enable)
{
	unsigned int address;

	/*
	 * The loader is allowed to leave the firmware cursor disabled.  Program
	 * CSRFORM explicitly so that taking over the console always restores a
	 * visible 16-raster blinking cursor.  Clearing bit 7 hides it when the VT
	 * core asks us to undraw the cursor.
	 */
	if (!pc98_gdc_write(PC98_GDC_COMMAND, PC98_GDC_CSRFORM) ||
	    !pc98_gdc_write(PC98_GDC_PARAMETER,
			      PC98_CURSOR_RASTER | (enable ? 0x80 : 0)) ||
	    !pc98_gdc_write(PC98_GDC_PARAMETER, PC98_CURSOR_START) ||
	    !pc98_gdc_write(PC98_GDC_PARAMETER, PC98_CURSOR_END) ||
	    !enable)
		return;

	address = vc->state.y * PC98_COLUMNS + vc->state.x;
	if (!pc98_gdc_write(PC98_GDC_COMMAND, PC98_GDC_CSRW) ||
	    !pc98_gdc_write(PC98_GDC_PARAMETER, address & 0xff))
		return;
	pc98_gdc_write(PC98_GDC_PARAMETER, (address >> 8) & 0xff);
}

static bool pc98con_switch(struct vc_data *vc)
{
	pc98_foreground = vc;
	return true;
}

static bool pc98con_blank(struct vc_data *vc, enum vesa_blank_mode blank,
			  bool mode_switch)
{
	if (blank)
		pc98con_clear(vc, 0, 0, PC98_COLUMNS * PC98_LINES);
	return true;
}

static u8 pc98con_build_attr(struct vc_data *vc, u8 color,
			     enum vc_intensity intensity, bool blink,
			     bool underline, bool reverse, bool italic)
{
	u8 foreground = color & 0x07;
	u8 background = (color >> 4) & 0x07;
	u8 rgb = foreground;
	u8 attr = 0x01;

	/*
	 * PC-98 has one RGB colour plus reverse video, rather than independent
	 * VGA foreground/background nibbles.  A coloured background with black
	 * text is represented exactly by selecting that colour and reversing.
	 */
	if (!foreground && background) {
		rgb = background;
		reverse = !reverse;
	}
	if (rgb & 0x01)
		attr |= 0x20;
	if (rgb & 0x02)
		attr |= 0x80;
	if (rgb & 0x04)
		attr |= 0x40;
	if (blink)
		attr |= 0x02;
	if (reverse)
		attr |= 0x04;
	if (underline)
		attr |= 0x08;
	if (!(attr & 0xe0) && !reverse)
		attr |= 0xe0;

	return attr;
}

static void pc98con_invert_region(struct vc_data *vc, u16 *p, int count)
{
	while (count--) {
		scr_writew(scr_readw(p) ^ vc->vc_complement_mask, p);
		p++;
	}
}

static const struct consw pc98_con = {
	.owner = THIS_MODULE,
	.con_startup = pc98con_startup,
	.con_init = pc98con_init,
	.con_deinit = pc98con_deinit,
	.con_clear = pc98con_clear,
	.con_putc = pc98con_putc,
	.con_putcs = pc98con_putcs,
	.con_cursor = pc98con_cursor,
	.con_scroll = pc98con_scroll,
	.con_switch = pc98con_switch,
	.con_blank = pc98con_blank,
	.con_build_attr = pc98con_build_attr,
	.con_invert_region = pc98con_invert_region,
};

void __init pc98con_register_screen(void)
{
	conswitchp = &pc98_con;
}

static int __init pc98_console_init(void)
{
	int error;

	if (conswitchp == &pc98_con)
		return 0;

	console_lock();
	error = do_take_over_console(&pc98_con, 0, MAX_NR_CONSOLES - 1, 1);
	console_unlock();

	return error;
}
console_initcall(pc98_console_init);

MODULE_DESCRIPTION("NEC PC-9800 GDC text console");
MODULE_LICENSE("GPL");
