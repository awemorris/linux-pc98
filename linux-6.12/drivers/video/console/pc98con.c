// SPDX-License-Identifier: GPL-2.0
/*
 * Text console for the NEC PC-9800 GDC text screen.
 *
 * Unlike PC/AT (vgacon), PC-98 keeps the character codes and the attributes in
 * two separate 16 KiB planes, both with a 2-byte stride: char at 0xA0000, attr
 * at 0xA2000. A cell at (x, y) is char plane word [y*80 + x] (ANK code in the
 * low byte) and attr plane byte [2*(y*80 + x)]. The attribute byte is
 * bit0 display, bit1 blink, bit2 reverse, bit3 underline, bit5 B, bit6 R,
 * bit7 G — so this driver keeps its own attribute encoding via con_build_attr
 * and splits every VT cell across the two planes.
 *
 * The cursor is the master GDC's own: commands go to I/O 0x62, their parameters
 * to 0x60. CSRW moves it, CSRFORM turns it on and off — and CSRFORM also
 * carries the character cell height, which must be left alone or the whole text
 * display changes shape, so it is taken from the BIOS work area.
 */
#include <linux/console.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/kd.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/vt_kern.h>
#include <linux/vt_buffer.h>

#include <asm/io.h>

#define PC98_COLS	80
#define PC98_ROWS	25

/* master (text) GDC */
#define GDC_PARAM	0x60		/* command parameters */
#define GDC_CMD		0x62		/* commands / data */
#define GDC_CSRW	0x49		/* set cursor address, 3 parameters */
#define GDC_CSRFORM	0x4b		/* cursor shape/enable, 3 parameters */
#define GDC_STAT_FIFO_FULL 0x02

#define GDC_FIFO_TIMEOUT_US 1000
#define PC98_WAIT_PORT	0x5f		/* 0.6 us fixed recovery time */

#define BIOS_CRT_RASTER	0x53b		/* BIOS work area: lines per row - 1 */

static u8 __iomem *pc98_cram;		/* char plane base (0xA0000) */
static u8 __iomem *pc98_aram;		/* attr plane base (0xA2000) */
static struct vc_data *pc98_display_fg;
static u8 pc98_cursor_lr = 15;		/* lines per character row - 1 */
static int pc98_cursor_on = -1;		/* -1 = never programmed */
static unsigned int pc98_cursor_pos = ~0U;

static inline void pc98_put(unsigned int pos, u16 ca)
{
	writew(ca & 0x00ff, pc98_cram + 2 * pos);	/* ANK char, high byte 0 */
	writeb((ca >> 8) & 0xff, pc98_aram + 2 * pos);	/* PC-98 attribute */
}

static inline u16 pc98_get(unsigned int pos)
{
	u16 ch = readw(pc98_cram + 2 * pos) & 0x00ff;
	u16 at = readb(pc98_aram + 2 * pos);

	return (at << 8) | ch;
}

static void pc98_fill(unsigned int pos, unsigned int count, u16 ca)
{
	while (count--)
		pc98_put(pos++, ca);
}

static const char *pc98con_startup(void)
{
	u8 lr;

	pc98_cram = (u8 __iomem *)(__ISA_IO_base + 0xa0000);
	pc98_aram = (u8 __iomem *)(__ISA_IO_base + 0xa2000);

	/* CSRFORM carries the cell height as well as the cursor shape, so reuse
	 * whatever the firmware set up rather than imposing one. */
	lr = readb((void __iomem *)(__ISA_IO_base + BIOS_CRT_RASTER)) & 0x1f;
	if (lr >= 7)
		pc98_cursor_lr = lr;

	return "PC-98-TEXT";
}

static void pc98con_init(struct vc_data *c, bool init)
{
	c->vc_can_do_color = true;
	c->vc_complement_mask = 0x0400;		/* reverse bit, for cursor/selection */
	c->vc_display_fg = &pc98_display_fg;

	if (init) {
		c->vc_cols = PC98_COLS;
		c->vc_rows = PC98_ROWS;
	} else {
		vc_resize(c, PC98_COLS, PC98_ROWS);
	}

	if (!pc98_display_fg)
		pc98_display_fg = c;
}

static void pc98con_deinit(struct vc_data *c)
{
	if (pc98_display_fg == c)
		pc98_display_fg = NULL;
}

static u8 pc98con_build_attr(struct vc_data *c, u8 color,
			     enum vc_intensity intensity,
			     bool blink, bool underline, bool reverse, bool italic)
{
	u8 fg = color & 0x0f;
	u8 a = 0x01;				/* display on */

	if (fg & 0x01)			/* VGA blue  -> PC-98 bit5 */
		a |= 0x20;
	if (fg & 0x02)			/* VGA green -> PC-98 bit7 */
		a |= 0x80;
	if (fg & 0x04)			/* VGA red   -> PC-98 bit6 */
		a |= 0x40;
	if (underline)
		a |= 0x08;
	if (reverse)
		a |= 0x04;
	if (blink)
		a |= 0x02;
	if (!(a & 0xe0) && !reverse)	/* black-on-black would be invisible */
		a |= 0xe0;
	return a;
}

static void pc98con_putc(struct vc_data *c, u16 ca, unsigned int y, unsigned int x)
{
	pc98_put(y * PC98_COLS + x, ca);
}

static void pc98con_putcs(struct vc_data *c, const u16 *s, unsigned int count,
			  unsigned int y, unsigned int x)
{
	unsigned int pos = y * PC98_COLS + x;

	while (count--)
		pc98_put(pos++, scr_readw(s++));
}

static void pc98con_clear(struct vc_data *c, unsigned int y, unsigned int x,
			  unsigned int count)
{
	pc98_fill(y * PC98_COLS + x, count, c->vc_video_erase_char);
}

static bool pc98con_switch(struct vc_data *c)
{
	return true;				/* redraw from the VT buffer */
}

static bool pc98con_blank(struct vc_data *c, enum vesa_blank_mode blank,
			  bool mode_switch)
{
	if (blank)
		pc98_fill(0, PC98_COLS * PC98_ROWS, c->vc_video_erase_char);
	return true;				/* console.c restores by redraw */
}

/*
 * Both commands and parameters enter the uPD7220's 16-byte FIFO.  Checking
 * FIFO FULL alone is not sufficient: the controller also requires at least
 * four 2xWCLK cycles between bytes.  A fast P6 can otherwise overrun the
 * real GDC even though the same sequence happens to work on an i486 or QEMU.
 * Two accesses to the PC-98 fixed-delay port provide about 1.2 us of
 * CPU-independent recovery time.
 *
 * Status is read from the parameter port, not the command/data port.
 */
static bool pc98_gdc_write(u16 port, u8 value)
{
	unsigned int timeout;

	for (timeout = 0; timeout < GDC_FIFO_TIMEOUT_US; timeout++) {
		if (!(inb(GDC_PARAM) & GDC_STAT_FIFO_FULL)) {
			outb(value, port);
			outb(0, PC98_WAIT_PORT);
			outb(0, PC98_WAIT_PORT);
			return true;
		}
		udelay(1);
	}

	return false;
}

static void pc98_gdc_cmd(u8 cmd, u8 p1, u8 p2, u8 p3)
{
	if (!pc98_gdc_write(GDC_CMD, cmd))
		return;
	if (!pc98_gdc_write(GDC_PARAM, p1))
		return;
	if (!pc98_gdc_write(GDC_PARAM, p2))
		return;
	pc98_gdc_write(GDC_PARAM, p3);
}

static void pc98_cursor_form(bool on)
{
	u8 lr = pc98_cursor_lr;

	/* p1: display bit + lines per row, p2: blink rate low + top line,
	 * p3: bottom line + blink rate high. A full-height blinking block. */
	pc98_gdc_cmd(GDC_CSRFORM, (on ? 0x80 : 0x00) | lr, 0xc0, (lr << 3) | 0x03);
}

static void pc98_cursor_move(unsigned int pos)
{
	pc98_gdc_cmd(GDC_CSRW, pos & 0xff, (pos >> 8) & 0xff, (pos >> 16) & 0x03);
}

static void pc98con_cursor(struct vc_data *c, bool enable)
{
	unsigned int pos = c->state.y * PC98_COLS + c->state.x;

	if (enable && pos != pc98_cursor_pos) {
		pc98_cursor_move(pos);
		pc98_cursor_pos = pos;
	}
	if (pc98_cursor_on != enable) {
		pc98_cursor_form(enable);
		pc98_cursor_on = enable;
	}
}

static bool pc98con_scroll(struct vc_data *c, unsigned int t, unsigned int b,
			   enum con_scroll dir, unsigned int lines)
{
	u16 erase = c->vc_video_erase_char;
	unsigned int i, n;

	if (!lines)
		return false;
	if (lines > b - t)
		lines = b - t;

	n = (b - t - lines) * PC98_COLS;

	switch (dir) {
	case SM_UP:
		for (i = 0; i < n; i++)
			pc98_put(t * PC98_COLS + i,
				 pc98_get((t + lines) * PC98_COLS + i));
		pc98_fill((b - lines) * PC98_COLS, lines * PC98_COLS, erase);
		break;
	case SM_DOWN:
		for (i = n; i-- > 0; )
			pc98_put((t + lines) * PC98_COLS + i,
				 pc98_get(t * PC98_COLS + i));
		pc98_fill(t * PC98_COLS, lines * PC98_COLS, erase);
		break;
	}
	return false;				/* did the scroll in VRAM */
}

static const struct consw pc98_con = {
	.owner		= THIS_MODULE,
	.con_startup	= pc98con_startup,
	.con_init	= pc98con_init,
	.con_deinit	= pc98con_deinit,
	.con_clear	= pc98con_clear,
	.con_putc	= pc98con_putc,
	.con_putcs	= pc98con_putcs,
	.con_cursor	= pc98con_cursor,
	.con_scroll	= pc98con_scroll,
	.con_switch	= pc98con_switch,
	.con_blank	= pc98con_blank,
	.con_build_attr	= pc98con_build_attr,
};

static int __init pc98con_console_init(void)
{
	int err;

	console_lock();
	err = do_take_over_console(&pc98_con, 0, MAX_NR_CONSOLES - 1, 1);
	console_unlock();
	return err;
}
module_init(pc98con_console_init);

MODULE_DESCRIPTION("NEC PC-9800 GDC text console");
MODULE_LICENSE("GPL");
