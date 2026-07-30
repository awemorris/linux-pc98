// SPDX-License-Identifier: GPL-2.0-only
/*
 * NEC PC-9821 Core-Graph Cirrus framebuffer
 *
 * Core-Graph exposes its GD5440 through the PC-98 fixed control ports,
 * relocated VGA registers and a selectable high linear aperture. It is not
 * a PCI Cirrus function, so the generic cirrusfb PCI probe cannot bind it.
 *
 * The PC-98 routing sequence and 640x480 mode stream are based on the
 * zlib-licensed StratoHAL 98disp_cirrus.c implementation by Awe Morris and
 * Keiichi Tabata.
 */

#include <linux/fb.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/module.h>
#include <linux/spinlock.h>

#define DRV_NAME		"pc98cirrusfb"

#define WAB_INDEX		0x0faa
#define WAB_DATA		0x0fab
#define WAB_REG_ID		0x00
#define WAB_REG_LINEAR		0x02
#define WAB_REG_RELAY		0x03
#define COREGRAPH_ID		0x5b

#define PC98_GDC_MODE		0x0068
#define PC98_VRAM_SWITCH	0x006a
#define PC98_WAIT		0x005f
#define CIRRUS_SLEEP		0x0ca3

#define CIRRUS_IO		0x0ca0
#define CIRRUS_CRTC		0x0da4
#define CIRRUS_STATUS		0x0daa

#define PC98CIRRUS_LFB_PHYS	0xf0000000UL
#define PC98CIRRUS_LFB_SIZE	(1024 * 1024)
#define PC98CIRRUS_WIDTH	640
#define PC98CIRRUS_HEIGHT	480
#define PC98CIRRUS_PITCH	640

struct pc98cirrusfb {
	struct fb_info *info;
	void __iomem *vram;
	spinlock_t reg_lock;
	u8 saved_sleep;
	u8 saved_relay;
	u8 saved_linear;
};

static struct pc98cirrusfb *pc98cirrus;

static void wab_write(u8 index, u8 value)
{
	outb(index, WAB_INDEX);
	outb(value, WAB_DATA);
}

static u8 wab_read(u8 index)
{
	outb(index, WAB_INDEX);
	return inb(WAB_DATA);
}

static void cl_seq_write(u8 index, u8 value)
{
	outb(index, CIRRUS_IO + 4);
	outb(value, CIRRUS_IO + 5);
}

static u8 cl_seq_read(u8 index)
{
	outb(index, CIRRUS_IO + 4);
	return inb(CIRRUS_IO + 5);
}

static void gfx_write(u8 index, u8 value)
{
	outb(index, CIRRUS_IO + 0x0e);
	outb(value, CIRRUS_IO + 0x0f);
}

static void crtc_write(u8 index, u8 value)
{
	outb(index, CIRRUS_CRTC);
	outb(value, CIRRUS_CRTC + 1);
}

static u8 crtc_read(u8 index)
{
	outb(index, CIRRUS_CRTC);
	return inb(CIRRUS_CRTC + 1);
}

static void attr_write(u8 index, u8 value)
{
	inb(CIRRUS_STATUS);
	outb(index, CIRRUS_IO);
	outb(value, CIRRUS_IO);
}

static void hidden_dac_write(u8 value)
{
	inb(CIRRUS_IO + 8);
	inb(CIRRUS_IO + 6);
	inb(CIRRUS_IO + 6);
	inb(CIRRUS_IO + 6);
	inb(CIRRUS_IO + 6);
	outb(value, CIRRUS_IO + 6);
}

static void dac_write(unsigned int index, u8 red, u8 green, u8 blue)
{
	outb(index, CIRRUS_IO + 8);
	outb(red, CIRRUS_IO + 9);
	outb(green, CIRRUS_IO + 9);
	outb(blue, CIRRUS_IO + 9);
}

static void pc98cirrus_load_rgb332_palette(void)
{
	unsigned int i;

	outb(0xff, CIRRUS_IO + 6);
	for (i = 0; i < 256; i++) {
		u8 r = (i >> 5) & 7;
		u8 g = (i >> 2) & 7;
		u8 b = i & 3;

		dac_write(i, r * 63 / 7, g * 63 / 7, b * 63 / 3);
	}
}

static void pc98cirrus_gate_enter(void)
{
	outb(0x0e, PC98_GDC_MODE);
	outb(0x07, PC98_VRAM_SWITCH);
	outb(0x8f, PC98_VRAM_SWITCH);
	outb(0x06, PC98_VRAM_SWITCH);
	wab_write(WAB_REG_RELAY, 0x03);
	outb(0, PC98_WAIT);
	outb(0, PC98_WAIT);
	outb(1, CIRRUS_SLEEP);
}

static void pc98cirrus_gate_leave(void)
{
	outb(0, CIRRUS_SLEEP);
	wab_write(WAB_REG_RELAY, 0);
	outb(0, PC98_WAIT);
	outb(0x07, PC98_VRAM_SWITCH);
	outb(0x8e, PC98_VRAM_SWITCH);
	outb(0x06, PC98_VRAM_SWITCH);
	outb(0, PC98_WAIT);
	outb(0x0f, PC98_GDC_MODE);
}

static int pc98cirrus_set_mode(void)
{
	static const u8 seq_index[] = {
		0x00, 0x01, 0x02, 0x03, 0x04, 0x07, 0x08,
		0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x16, 0x18,
		0x1b, 0x1c, 0x1d, 0x1e, 0x1f
	};
	static const u8 seq_value[] = {
		0x01, 0x01, 0x0f, 0x00, 0x0e, 0x11, 0x00,
		0x66, 0x48, 0x56, 0x60, 0x30, 0x58, 0x40,
		0x3b, 0x23, 0x3d, 0x3b, 0x20
	};
	static const u8 crtc_value[0x1c] = {
		0x5f, 0x4f, 0x50, 0x84, 0x54, 0x80, 0x0b, 0x3e,
		0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0xe5, 0x87, 0xdf, 0x50, 0x00, 0xe7, 0x04, 0xe3,
		0xff, 0x00, 0x90, 0x22
	};
	static const u8 gfx_value[] = {
		0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x05, 0x0f, 0xff
	};
	static const u8 attr_value[] = {
		0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
		0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
		0x41, 0x00, 0x0f, 0x00, 0x00
	};
	unsigned int i;

	gfx_write(0x33, 0);
	gfx_write(0x31, 0x04);
	gfx_write(0x31, 0);

	cl_seq_write(0x06, 0x12);
	cl_seq_write(0x12, 0);
	if (crtc_read(0x27) != 0xa0)
		return -ENODEV;

	for (i = 0; i < ARRAY_SIZE(seq_index); i++)
		cl_seq_write(seq_index[i], seq_value[i]);
	cl_seq_write(0x0f, (cl_seq_read(0x0f) & 0xdf) | 0x20);

	outb(0xe3, CIRRUS_IO + 2);
	gfx_write(0x06, 0x05);
	cl_seq_write(0x00, 0x03);

	crtc_write(0x11, 0x20);
	for (i = 0; i < ARRAY_SIZE(crtc_value); i++)
		crtc_write(i, crtc_value[i]);
	for (i = 0; i < ARRAY_SIZE(gfx_value); i++)
		gfx_write(i, gfx_value[i]);

	inb(CIRRUS_STATUS);
	for (i = 0; i < ARRAY_SIZE(attr_value); i++)
		attr_write(i, attr_value[i]);
	inb(CIRRUS_STATUS);
	outb(0x20, CIRRUS_IO);

	hidden_dac_write(0x20);
	outb(0xff, CIRRUS_IO + 6);
	gfx_write(0x09, 0);
	gfx_write(0x0a, 0);
	gfx_write(0x0b, 0x21);

	cl_seq_write(0x17, cl_seq_read(0x17) | 0x44);
	cl_seq_write(0x18, cl_seq_read(0x18) & 0xbf);
	gfx_write(0x31, 0x04);
	gfx_write(0x31, 0);
	pc98cirrus_load_rgb332_palette();

	cl_seq_write(0x01, 0x21);
	return 0;
}

static int pc98cirrusfb_check_var(struct fb_var_screeninfo *var,
				  struct fb_info *info)
{
	if (var->xres != PC98CIRRUS_WIDTH ||
	    var->yres != PC98CIRRUS_HEIGHT ||
	    var->bits_per_pixel != 8)
		return -EINVAL;

	var->xres_virtual = PC98CIRRUS_WIDTH;
	var->yres_virtual = PC98CIRRUS_HEIGHT;
	var->red.offset = 5;
	var->red.length = 3;
	var->green.offset = 2;
	var->green.length = 3;
	var->blue.offset = 0;
	var->blue.length = 2;
	var->transp.length = 0;
	return 0;
}

static int pc98cirrusfb_setcolreg(unsigned int regno, unsigned int red,
				  unsigned int green, unsigned int blue,
				  unsigned int transp, struct fb_info *info)
{
	struct pc98cirrusfb *cfb = info->par;
	unsigned long flags;

	if (regno >= 256)
		return -EINVAL;

	spin_lock_irqsave(&cfb->reg_lock, flags);
	dac_write(regno, red >> 10, green >> 10, blue >> 10);
	spin_unlock_irqrestore(&cfb->reg_lock, flags);
	return 0;
}

static int pc98cirrusfb_blank(int blank, struct fb_info *info)
{
	struct pc98cirrusfb *cfb = info->par;
	unsigned long flags;
	u8 value;

	spin_lock_irqsave(&cfb->reg_lock, flags);
	value = cl_seq_read(0x01);
	if (blank == FB_BLANK_UNBLANK)
		value &= ~0x20;
	else
		value |= 0x20;
	cl_seq_write(0x01, value);
	spin_unlock_irqrestore(&cfb->reg_lock, flags);
	return 0;
}

static const struct fb_ops pc98cirrusfb_ops = {
	.owner		= THIS_MODULE,
	__FB_DEFAULT_IOMEM_OPS_RDWR,
	.fb_check_var	= pc98cirrusfb_check_var,
	.fb_setcolreg	= pc98cirrusfb_setcolreg,
	.fb_blank	= pc98cirrusfb_blank,
	.fb_fillrect	= cfb_fillrect,
	.fb_copyarea	= cfb_copyarea,
	.fb_imageblit	= cfb_imageblit,
	__FB_DEFAULT_IOMEM_OPS_MMAP,
};

static int __init pc98cirrusfb_init(void)
{
	struct pc98cirrusfb *cfb;
	struct fb_info *info;
	int ret;

	if (wab_read(WAB_REG_ID) != COREGRAPH_ID)
		return -ENODEV;

	info = framebuffer_alloc(sizeof(*cfb), NULL);
	if (!info)
		return -ENOMEM;
	cfb = info->par;
	cfb->info = info;
	spin_lock_init(&cfb->reg_lock);
	cfb->saved_sleep = inb(CIRRUS_SLEEP);
	cfb->saved_relay = wab_read(WAB_REG_RELAY);
	cfb->saved_linear = wab_read(WAB_REG_LINEAR);

	outb(cfb->saved_sleep | 1, CIRRUS_SLEEP);
	wab_write(WAB_REG_RELAY, 1);
	wab_write(WAB_REG_LINEAR, 0xf0);
	pc98cirrus_gate_enter();

	ret = pc98cirrus_set_mode();
	if (ret)
		goto err_route;

	if (!request_mem_region(PC98CIRRUS_LFB_PHYS, PC98CIRRUS_LFB_SIZE,
				DRV_NAME)) {
		ret = -EBUSY;
		goto err_route;
	}
	cfb->vram = ioremap(PC98CIRRUS_LFB_PHYS, PC98CIRRUS_LFB_SIZE);
	if (!cfb->vram) {
		ret = -ENOMEM;
		goto err_release_mem;
	}

	memset_io(cfb->vram, 0, PC98CIRRUS_PITCH * PC98CIRRUS_HEIGHT);
	cl_seq_write(0x01, 0x01);

	strscpy(info->fix.id, "PC98 CoreGraph", sizeof(info->fix.id));
	info->fix.smem_start = PC98CIRRUS_LFB_PHYS;
	info->fix.smem_len = PC98CIRRUS_LFB_SIZE;
	info->fix.type = FB_TYPE_PACKED_PIXELS;
	info->fix.visual = FB_VISUAL_PSEUDOCOLOR;
	info->fix.line_length = PC98CIRRUS_PITCH;
	info->fix.accel = FB_ACCEL_NONE;
	info->var.xres = PC98CIRRUS_WIDTH;
	info->var.yres = PC98CIRRUS_HEIGHT;
	info->var.xres_virtual = PC98CIRRUS_WIDTH;
	info->var.yres_virtual = PC98CIRRUS_HEIGHT;
	info->var.bits_per_pixel = 8;
	info->var.activate = FB_ACTIVATE_NOW;
	info->var.height = -1;
	info->var.width = -1;
	pc98cirrusfb_check_var(&info->var, info);
	info->screen_base = cfb->vram;
	info->screen_size = PC98CIRRUS_LFB_SIZE;
	info->fbops = &pc98cirrusfb_ops;
	info->flags = FBINFO_HWACCEL_DISABLED;

	ret = fb_alloc_cmap(&info->cmap, 256, 0);
	if (ret)
		goto err_unmap;
	ret = register_framebuffer(info);
	if (ret)
		goto err_cmap;

	pc98cirrus = cfb;
	pr_info(DRV_NAME ": fb%d: Core-Graph GD5440 at 0x%08lx, 640x480x8\n",
		info->node, PC98CIRRUS_LFB_PHYS);
	return 0;

err_cmap:
	fb_dealloc_cmap(&info->cmap);
err_unmap:
	iounmap(cfb->vram);
err_release_mem:
	release_mem_region(PC98CIRRUS_LFB_PHYS, PC98CIRRUS_LFB_SIZE);
err_route:
	pc98cirrus_gate_leave();
	framebuffer_release(info);
	return ret;
}

static void __exit pc98cirrusfb_exit(void)
{
	struct pc98cirrusfb *cfb = pc98cirrus;

	if (!cfb)
		return;
	unregister_framebuffer(cfb->info);
	fb_dealloc_cmap(&cfb->info->cmap);
	iounmap(cfb->vram);
	release_mem_region(PC98CIRRUS_LFB_PHYS, PC98CIRRUS_LFB_SIZE);
	pc98cirrus_gate_leave();
	if (cfb->saved_linear != 0 && cfb->saved_linear != 0xff)
		wab_write(WAB_REG_LINEAR, cfb->saved_linear);
	outb(cfb->saved_sleep, CIRRUS_SLEEP);
	wab_write(WAB_REG_RELAY, cfb->saved_relay);
	framebuffer_release(cfb->info);
	pc98cirrus = NULL;
}

module_init(pc98cirrusfb_init);
module_exit(pc98cirrusfb_exit);

MODULE_DESCRIPTION("NEC PC-9821 Core-Graph Cirrus framebuffer");
MODULE_AUTHOR("Awe Morris, Keiichi Tabata");
MODULE_LICENSE("GPL");
