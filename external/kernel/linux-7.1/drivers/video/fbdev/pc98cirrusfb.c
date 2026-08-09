// SPDX-License-Identifier: GPL-2.0-only
/*
 * NEC PC-9821 Core-Graph Cirrus framebuffer
 *
 * Core-Graph exposes its GD5440 through the PC-98 fixed control ports,
 * relocated VGA registers and a selectable high linear aperture. It is not
 * a PCI Cirrus function, so the generic cirrusfb PCI probe cannot bind it.
 *
 * The PC-98 routing sequence and 640x480 mode streams are based on the
 * zlib-licensed StratoHAL 98disp_cirrus.c implementation by Awe Morris and
 * Keiichi Tabata.  The 800x600 and 1024x768 timings follow the same Cirrus
 * register construction used by the generic Linux cirrusfb driver.
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
#define PC98CIRRUS_DEFAULT_MODE	"800x600-16"

struct pc98cirrus_mode {
	const char *name;
	u16 xres;
	u16 yres;
	u16 pitch;
	u8 bpp;
	u8 misc;
	u8 hdr;
	u32 pixclock;
	u16 left_margin;
	u16 right_margin;
	u16 upper_margin;
	u16 lower_margin;
	u16 hsync_len;
	u16 vsync_len;
	u32 sync;
	u8 seq[19];
	u8 crtc[0x1c];
};

/*
 * These are the three useful 1 MiB Core-Graph modes.  The 640x480 stream is
 * NEC's path-08h stream verbatim.  The two larger modes retain its board and
 * pixel-format programming, with standard VESA 60 Hz CRTC/VCLK timings.
 */
static const struct pc98cirrus_mode pc98cirrus_modes[] = {
	{
		.name = "640x480-24",
		.xres = 640, .yres = 480, .pitch = 2048, .bpp = 24,
		.misc = 0xe3, .hdr = 0xe5, .pixclock = 39721,
		.left_margin = 48, .right_margin = 16,
		.upper_margin = 33, .lower_margin = 10,
		.hsync_len = 96, .vsync_len = 2,
		.seq = {
			0x01, 0x01, 0x0f, 0x00, 0x0e, 0x15, 0x00,
			0x3a, 0x48, 0x56, 0x60, 0x30, 0x58, 0x40,
			0x16, 0x23, 0x3d, 0x3b, 0x20
		},
		.crtc = {
			0x5f, 0x4f, 0x50, 0x84, 0x53, 0x9f, 0x0b, 0x3e,
			0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0xe5, 0x87, 0xdf, 0x00, 0x00, 0xe7, 0x04, 0xe3,
			0xff, 0x00, 0x90, 0x32
		},
	}, {
		.name = "800x600-16",
		.xres = 800, .yres = 600, .pitch = 1600, .bpp = 16,
		.misc = 0x0f, .hdr = 0xe1, .pixclock = 25000,
		.left_margin = 88, .right_margin = 40,
		.upper_margin = 23, .lower_margin = 1,
		.hsync_len = 128, .vsync_len = 4,
		.sync = FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
		.seq = {
			0x01, 0x01, 0x0f, 0x00, 0x0e, 0x13, 0x00,
			0x6d, 0x48, 0x56, 0x51, 0x30, 0x58, 0x40,
			0x3e, 0x23, 0x3d, 0xba, 0x20
		},
		.crtc = {
			0x7f, 0x63, 0x64, 0x84, 0x6a, 0x1a, 0x72, 0xf0,
			0x00, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x59, 0x6d, 0x57, 0xc8, 0x00, 0x58, 0x72, 0xc3,
			0xff, 0x00, 0x20, 0x22
		},
	}, {
		.name = "1024x768-8",
		.xres = 1024, .yres = 768, .pitch = 1024, .bpp = 8,
		.misc = 0xcf, .hdr = 0x20, .pixclock = 15385,
		.left_margin = 160, .right_margin = 24,
		.upper_margin = 29, .lower_margin = 3,
		.hsync_len = 136, .vsync_len = 6,
		.seq = {
			0x01, 0x01, 0x0f, 0x00, 0x0e, 0x11, 0x00,
			0x66, 0x48, 0x56, 0x3b, 0x30, 0x58, 0x40,
			0x3b, 0x23, 0x3d, 0x9a, 0x20
		},
		.crtc = {
			0xa3, 0x7f, 0x80, 0x88, 0x84, 0x95, 0x24, 0xfd,
			0x00, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x02, 0x68, 0xff, 0x80, 0x00, 0x00, 0x24, 0xc3,
			0xff, 0x00, 0xe0, 0x22
		},
	},
};

struct pc98cirrusfb {
	struct fb_info *info;
	void __iomem *vram;
	/* Serializes indexed-register and palette access. */
	spinlock_t reg_lock;
	u8 saved_sleep;
	u8 saved_relay;
	u8 saved_linear;
	bool gate_active;
	const struct pc98cirrus_mode *mode;
	u32 pseudo_palette[16];
};

static struct pc98cirrusfb *pc98cirrus;
static char *mode_option = PC98CIRRUS_DEFAULT_MODE;
module_param_named(mode, mode_option, charp, 0444);
MODULE_PARM_DESC(mode, "Video mode: 640x480-24, 800x600-16 or 1024x768-8");

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

static u8 gfx_read(u8 index)
{
	outb(index, CIRRUS_IO + 0x0e);
	return inb(CIRRUS_IO + 0x0f);
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

static void hidden_dac_write(u8 value)
{
	inb(CIRRUS_IO + 8);
	inb(CIRRUS_IO + 6);
	inb(CIRRUS_IO + 6);
	inb(CIRRUS_IO + 6);
	inb(CIRRUS_IO + 6);
	outb(value, CIRRUS_IO + 6);
}

static u8 hidden_dac_read(void)
{
	u8 value;

	inb(CIRRUS_IO + 8);
	inb(CIRRUS_IO + 6);
	inb(CIRRUS_IO + 6);
	inb(CIRRUS_IO + 6);
	inb(CIRRUS_IO + 6);
	value = inb(CIRRUS_IO + 6);
	inb(CIRRUS_IO + 8);
	return value;
}

static u8 misc_read(void)
{
	return inb(CIRRUS_IO + 0x0c);
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
	unsigned int i;

	outb(0, CIRRUS_SLEEP);
	wab_write(WAB_REG_RELAY, 0);
	outb(0, PC98_WAIT);
	outb(0x07, PC98_VRAM_SWITCH);
	outb(0x8e, PC98_VRAM_SWITCH);
	outb(0x06, PC98_VRAM_SWITCH);
	for (i = 0; i < 200000; i++)
		outb(0, PC98_WAIT);
	outb(0x0f, PC98_GDC_MODE);
}

static void pc98cirrus_restore_board_state(struct pc98cirrusfb *cfb)
{
	if (cfb->gate_active) {
		pc98cirrus_gate_leave();
		cfb->gate_active = false;
	}
	wab_write(WAB_REG_LINEAR, cfb->saved_linear);
	wab_write(WAB_REG_RELAY, cfb->saved_relay);
	outb(cfb->saved_sleep, CIRRUS_SLEEP);
}

static bool pc98cirrus_validate_identity(void)
{
	u8 old_sr06 = cl_seq_read(0x06);
	u8 sr06;
	u8 cr27;

	cl_seq_write(0x06, 0x12);
	sr06 = cl_seq_read(0x06);
	cr27 = sr06 == 0x12 ? crtc_read(0x27) : 0xff;
	cl_seq_write(0x06, old_sr06);
	pr_info(DRV_NAME ": identity SR06=%02x CR27=%02x\n", sr06, cr27);
	return sr06 == 0x12 && cr27 != 0 && cr27 != 0xff;
}

static int pc98cirrus_set_mode(const struct pc98cirrus_mode *mode)
{
	static const u8 seq_index[] = {
		0x00, 0x01, 0x02, 0x03, 0x04, 0x07, 0x08,
		0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x16, 0x18,
		0x1b, 0x1c, 0x1d, 0x1e, 0x1f
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
	u8 cr27;

	gfx_write(0x33, 0);
	gfx_write(0x31, 0x04);
	gfx_write(0x31, 0);

	cl_seq_write(0x06, 0x12);
	cl_seq_write(0x12, 0);
	cr27 = crtc_read(0x27);
	if (cr27 == 0 || cr27 == 0xff)
		return -ENODEV;
	pr_info(DRV_NAME ": unlocked GD54xx extensions, CR27=%02x\n", cr27);

	for (i = 0; i < ARRAY_SIZE(seq_index); i++)
		cl_seq_write(seq_index[i], mode->seq[i]);
	cl_seq_write(0x0f, (cl_seq_read(0x0f) & 0xdf) | 0x20);

	outb(mode->misc, CIRRUS_IO + 2);
	gfx_write(0x06, 0x05);
	cl_seq_write(0x00, 0x03);

	crtc_write(0x11, 0x20);
	for (i = 0; i < ARRAY_SIZE(mode->crtc); i++)
		crtc_write(i, mode->crtc[i]);
	for (i = 0; i < ARRAY_SIZE(gfx_value); i++)
		gfx_write(i, gfx_value[i]);

	/*
	 * Match the NEC stream exactly: reset the flip-flop once, then emit
	 * uninterrupted index/data pairs before enabling attribute video.
	 */
	inb(CIRRUS_STATUS);
	for (i = 0; i < ARRAY_SIZE(attr_value); i++) {
		outb(i, CIRRUS_IO);
		outb(attr_value[i], CIRRUS_IO);
	}
	inb(CIRRUS_STATUS);
	outb(0x20, CIRRUS_IO);

	hidden_dac_write(mode->hdr);
	outb(0xff, CIRRUS_IO + 6);
	gfx_write(0x09, 0);
	gfx_write(0x0a, 0);
	gfx_write(0x0b, 0x21);

	cl_seq_write(0x17, cl_seq_read(0x17) | 0x44);
	cl_seq_write(0x18, cl_seq_read(0x18) & 0xbf);
	gfx_write(0x31, 0x04);
	gfx_write(0x31, 0);
	if (mode->bpp == 8)
		pc98cirrus_load_rgb332_palette();

	cl_seq_write(0x01, 0x21);
	cr27 = crtc_read(0x27);
	if (cr27 == 0 || cr27 == 0xff)
		return -ENODEV;
	pr_info(DRV_NAME ": %s: SR07=%02x SR0e=%02x SR17=%02x SR18=%02x\n",
		mode->name,
		cl_seq_read(0x07), cl_seq_read(0x0e), cl_seq_read(0x17),
		cl_seq_read(0x18));
	pr_info(DRV_NAME ": MISC=%02x CR13=%02x CR1b=%02x GR0b=%02x GR31=%02x HDR=%02x\n",
		misc_read(), crtc_read(0x13),
		crtc_read(0x1b), gfx_read(0x0b), gfx_read(0x31),
		hidden_dac_read());
	return 0;
}

static const struct pc98cirrus_mode *
pc98cirrus_find_mode(unsigned int xres, unsigned int yres,
		     unsigned int bpp)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(pc98cirrus_modes); i++) {
		const struct pc98cirrus_mode *mode = &pc98cirrus_modes[i];

		if (mode->xres == xres && mode->yres == yres &&
		    mode->bpp == bpp)
			return mode;
	}
	return NULL;
}

static const struct pc98cirrus_mode *pc98cirrus_find_mode_name(const char *name)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(pc98cirrus_modes); i++)
		if (!strcmp(name, pc98cirrus_modes[i].name))
			return &pc98cirrus_modes[i];
	return NULL;
}

static void pc98cirrus_set_var(struct fb_var_screeninfo *var,
			       const struct pc98cirrus_mode *mode)
{
	var->xres = mode->xres;
	var->yres = mode->yres;
	var->xres_virtual = mode->xres;
	var->yres_virtual = mode->yres;
	var->xoffset = 0;
	var->yoffset = 0;
	var->bits_per_pixel = mode->bpp;
	var->pixclock = mode->pixclock;
	var->left_margin = mode->left_margin;
	var->right_margin = mode->right_margin;
	var->upper_margin = mode->upper_margin;
	var->lower_margin = mode->lower_margin;
	var->hsync_len = mode->hsync_len;
	var->vsync_len = mode->vsync_len;
	var->sync = mode->sync;
	var->vmode = FB_VMODE_NONINTERLACED;
	var->red.msb_right = 0;
	var->green.msb_right = 0;
	var->blue.msb_right = 0;
	var->transp.offset = 0;
	var->transp.length = 0;
	var->transp.msb_right = 0;

	if (mode->bpp == 8) {
		var->red.offset = 0;
		var->green.offset = 0;
		var->blue.offset = 0;
		var->red.length = 8;
		var->green.length = 8;
		var->blue.length = 8;
	} else if (mode->bpp == 16) {
		var->red.offset = 11;
		var->green.offset = 5;
		var->blue.offset = 0;
		var->red.length = 5;
		var->green.length = 6;
		var->blue.length = 5;
	} else {
		var->red.offset = 16;
		var->green.offset = 8;
		var->blue.offset = 0;
		var->red.length = 8;
		var->green.length = 8;
		var->blue.length = 8;
	}
}

static int pc98cirrusfb_check_var(struct fb_var_screeninfo *var,
				  struct fb_info *info)
{
	const struct pc98cirrus_mode *mode;

	mode = pc98cirrus_find_mode(var->xres, var->yres,
				    var->bits_per_pixel);
	if (!mode)
		return -EINVAL;
	pc98cirrus_set_var(var, mode);
	return 0;
}

static int pc98cirrusfb_set_par(struct fb_info *info)
{
	struct pc98cirrusfb *cfb = info->par;
	const struct pc98cirrus_mode *mode;
	unsigned long flags;
	int ret;

	mode = pc98cirrus_find_mode(info->var.xres, info->var.yres,
				    info->var.bits_per_pixel);
	if (!mode)
		return -EINVAL;
	if (mode == cfb->mode) {
		info->fix.visual = mode->bpp == 8 ? FB_VISUAL_PSEUDOCOLOR :
						       FB_VISUAL_TRUECOLOR;
		info->fix.line_length = mode->pitch;
		return 0;
	}

	spin_lock_irqsave(&cfb->reg_lock, flags);
	ret = pc98cirrus_set_mode(mode);
	spin_unlock_irqrestore(&cfb->reg_lock, flags);
	if (ret)
		return ret;

	memset_io(cfb->vram, 0, mode->pitch * mode->yres);
	spin_lock_irqsave(&cfb->reg_lock, flags);
	cl_seq_write(0x01, 0x01);
	spin_unlock_irqrestore(&cfb->reg_lock, flags);

	cfb->mode = mode;
	info->fix.visual = mode->bpp == 8 ? FB_VISUAL_PSEUDOCOLOR :
					       FB_VISUAL_TRUECOLOR;
	info->fix.line_length = mode->pitch;
	return 0;
}

static int pc98cirrusfb_setcolreg(unsigned int regno, unsigned int red,
				  unsigned int green, unsigned int blue,
				  unsigned int transp, struct fb_info *info)
{
	struct pc98cirrusfb *cfb = info->par;
	unsigned long flags;
	u32 value;

	if (info->fix.visual == FB_VISUAL_TRUECOLOR) {
		if (regno >= ARRAY_SIZE(cfb->pseudo_palette))
			return -EINVAL;
		value = ((red >> (16 - info->var.red.length)) <<
			 info->var.red.offset) |
			((green >> (16 - info->var.green.length)) <<
			 info->var.green.offset) |
			((blue >> (16 - info->var.blue.length)) <<
			 info->var.blue.offset);
		cfb->pseudo_palette[regno] = value;
		return 0;
	}
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
	.fb_set_par	= pc98cirrusfb_set_par,
	.fb_setcolreg	= pc98cirrusfb_setcolreg,
	.fb_blank	= pc98cirrusfb_blank,
	.fb_fillrect	= cfb_fillrect,
	.fb_copyarea	= cfb_copyarea,
	.fb_imageblit	= cfb_imageblit,
	__FB_DEFAULT_IOMEM_OPS_MMAP,
};

static int __init pc98cirrusfb_init(void)
{
	const struct pc98cirrus_mode *mode;
	const char *selected_mode = mode_option;
	struct pc98cirrusfb *cfb;
	struct fb_info *info;
	char *video_option = NULL;
	u8 coregraph_id;
	int ret;

	if (fb_get_options(DRV_NAME, &video_option))
		return -ENODEV;
	if (video_option && *video_option)
		selected_mode = video_option;
	mode = pc98cirrus_find_mode_name(selected_mode);
	if (!mode) {
		pr_warn(DRV_NAME ": unsupported mode '%s'; using %s\n",
			selected_mode, PC98CIRRUS_DEFAULT_MODE);
		mode = pc98cirrus_find_mode_name(PC98CIRRUS_DEFAULT_MODE);
	}

	coregraph_id = wab_read(WAB_REG_ID);
	if (coregraph_id != COREGRAPH_ID)
		return -ENODEV;

	info = framebuffer_alloc(sizeof(*cfb), NULL);
	if (!info)
		return -ENOMEM;
	cfb = info->par;
	cfb->info = info;
	cfb->mode = mode;
	spin_lock_init(&cfb->reg_lock);
	cfb->saved_sleep = inb(CIRRUS_SLEEP);
	cfb->saved_relay = wab_read(WAB_REG_RELAY);
	cfb->saved_linear = wab_read(WAB_REG_LINEAR);
	pr_info(DRV_NAME ": Core-Graph ID=%02x reg02=%02x reg03=%02x\n",
		coregraph_id, cfb->saved_linear, cfb->saved_relay);
	pr_info(DRV_NAME ": saved sleep=%02x\n", cfb->saved_sleep);

	outb(cfb->saved_sleep | 1, CIRRUS_SLEEP);
	wab_write(WAB_REG_RELAY, 1);
	if (!pc98cirrus_validate_identity()) {
		ret = -ENODEV;
		goto err_restore;
	}
	wab_write(WAB_REG_LINEAR, 0xf0);
	if (wab_read(WAB_REG_LINEAR) != 0xf0) {
		pr_err(DRV_NAME ": reg02 did not retain f0; no linear aperture\n");
		ret = -EIO;
		goto err_restore;
	}
	pc98cirrus_gate_enter();
	cfb->gate_active = true;

	ret = pc98cirrus_set_mode(mode);
	if (ret)
		goto err_restore;

	if (!request_mem_region(PC98CIRRUS_LFB_PHYS, PC98CIRRUS_LFB_SIZE,
				DRV_NAME)) {
		ret = -EBUSY;
		goto err_restore;
	}
	cfb->vram = ioremap(PC98CIRRUS_LFB_PHYS, PC98CIRRUS_LFB_SIZE);
	if (!cfb->vram) {
		ret = -ENOMEM;
		goto err_release_mem;
	}

	memset_io(cfb->vram, 0, mode->pitch * mode->yres);
	cl_seq_write(0x01, 0x01);

	strscpy(info->fix.id, "PC98 CoreGraph", sizeof(info->fix.id));
	info->fix.smem_start = PC98CIRRUS_LFB_PHYS;
	info->fix.smem_len = PC98CIRRUS_LFB_SIZE;
	info->fix.type = FB_TYPE_PACKED_PIXELS;
	info->fix.visual = mode->bpp == 8 ? FB_VISUAL_PSEUDOCOLOR :
					      FB_VISUAL_TRUECOLOR;
	info->fix.line_length = mode->pitch;
	info->fix.accel = FB_ACCEL_NONE;
	pc98cirrus_set_var(&info->var, mode);
	info->var.activate = FB_ACTIVATE_NOW;
	info->var.height = -1;
	info->var.width = -1;
	info->screen_base = cfb->vram;
	info->screen_size = PC98CIRRUS_LFB_SIZE;
	info->fbops = &pc98cirrusfb_ops;
	info->pseudo_palette = cfb->pseudo_palette;
	info->flags = FBINFO_HWACCEL_DISABLED;

	ret = fb_alloc_cmap(&info->cmap, 256, 0);
	if (ret)
		goto err_unmap;
	ret = register_framebuffer(info);
	if (ret)
		goto err_cmap;

	pc98cirrus = cfb;
	pr_info(DRV_NAME ": fb%d: Core-Graph GD5440 at 0x%08lx, %ux%ux%u, pitch %u\n",
		info->node, PC98CIRRUS_LFB_PHYS, mode->xres, mode->yres,
		mode->bpp, mode->pitch);
	return 0;

err_cmap:
	fb_dealloc_cmap(&info->cmap);
err_unmap:
	iounmap(cfb->vram);
err_release_mem:
	release_mem_region(PC98CIRRUS_LFB_PHYS, PC98CIRRUS_LFB_SIZE);
err_restore:
	pc98cirrus_restore_board_state(cfb);
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
	pc98cirrus_restore_board_state(cfb);
	framebuffer_release(cfb->info);
	pc98cirrus = NULL;
}

module_init(pc98cirrusfb_init);
module_exit(pc98cirrusfb_exit);

/* Deliberately no MODULE_ALIAS: untested PC-98 variants require manual load. */
MODULE_DESCRIPTION("NEC PC-9821 Core-Graph Cirrus framebuffer");
MODULE_AUTHOR("Awe Morris, Keiichi Tabata");
MODULE_LICENSE("GPL");
