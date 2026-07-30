// SPDX-License-Identifier: GPL-2.0-only
/*
 * NEC PC-9821 built-in Trident TGUI96xx framebuffer
 *
 * This is an altered Linux adaptation of StratoHAL's zlib-licensed
 * 98disp_trident.c by Awe Morris and Keiichi Tabata.  It intentionally keeps
 * only the field-proven PC-98 register access, 640x480x8 mode, fetch state and
 * relay sequence.  The diagnostic experiments and graphics-engine bring-up
 * remain in the original source until they can be tested on real hardware.
 *
 * Original work:
 * Copyright (c) 2025-2026 Awe Morris
 * Copyright (c) 1996-2024 Keiichi Tabata
 *
 * Target machines include NEC Mate R Ra43/Ra33/Ra266/Ra300 systems with the
 * on-board PCI TGUI9660/9680/9682 (PCI 1023:9660).
 */

#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <linux/sizes.h>
#include <linux/spinlock.h>
#include <linux/vmalloc.h>

#define DRV_NAME		"pc98tridentfb"

#define PCI_VENDOR_TRIDENT	0x1023
#define PCI_DEVICE_TGUI9660	0x9660

#define TG_VGA_BASE		0x03c0
#define TG_CRTC_COLOR		0x03d4
#define TG_STATUS_COLOR		0x03da
#define TG_CRTC_MONO		0x03b4
#define TG_STATUS_MONO		0x03ba
#define TG_VCLK			0x43c8
#define TG_SDAC			0x83c8

#define PC98_GDC_MODE		0x0068
#define PC98_WAIT		0x005f
#define PC98_RELAY		0x0fac

#define TG_WIDTH		640
#define TG_HEIGHT		480
#define TG_BPP			8
#define TG_PITCH		1024
#define TG_FB_SIZE		(TG_PITCH * TG_HEIGHT)
#define TG_BAR1_MIN_SIZE	0x10000
#define TG_VERIFY_PASSES	8

static unsigned long fb_phys;
module_param(fb_phys, ulong, 0444);
MODULE_PARM_DESC(fb_phys,
		 "Physical framebuffer override (0 selects BAR0/CR21 automatically)");

static bool allow_pc98_wakeup;
module_param(allow_pc98_wakeup, bool, 0444);
MODULE_PARM_DESC(allow_pc98_wakeup,
		 "Allow the last-resort port 0x94 wakeup (can touch the FDC)");

struct pc98trident_saved {
	u8 misc;
	u8 sr[0x10];
	u8 sr0d_old;
	u8 sr0d_new;
	u8 sr0e_new;
	u8 crtc[0x51];
	u8 gfx[0x70];
	u8 attr[0x15];
	u8 hidden_dac;
	u8 dac_mask;
	u8 dac[256 * 3];
	u8 vclk_lo;
	u8 vclk_hi;
	u8 sdac04;
	bool valid;
};

struct pc98tridentfb {
	struct pci_dev *pdev;
	struct fb_info *info;
	void __iomem *regs;
	void __iomem *vram;
	u8 *shadow;
	resource_size_t regs_phys;
	resource_size_t fb_phys;
	resource_size_t vram_size;
	u16 crtc;
	u16 status;
	u8 chip_rev;
	bool mmio;
	bool pio_claimed;
	bool bar1_claimed;
	bool bar0_claimed;
	bool fixed_fb_claimed;
	bool relay_active;
	spinlock_t reg_lock;
	struct mutex vram_lock;
	struct pc98trident_saved saved;
};

static inline u8 tg_read(struct pc98tridentfb *tfb, unsigned int port)
{
	if (tfb->mmio)
		return readb(tfb->regs + port);
	return inb(port);
}

static inline void tg_write(struct pc98tridentfb *tfb, unsigned int port,
			    u8 value)
{
	if (tfb->mmio)
		writeb(value, tfb->regs + port);
	else
		outb(value, port);
}

static void tg_select_crtc(struct pc98tridentfb *tfb, u8 misc)
{
	if (misc & 1) {
		tfb->crtc = TG_CRTC_COLOR;
		tfb->status = TG_STATUS_COLOR;
	} else {
		tfb->crtc = TG_CRTC_MONO;
		tfb->status = TG_STATUS_MONO;
	}
}

static u8 tg_misc_read(struct pc98tridentfb *tfb)
{
	return tg_read(tfb, TG_VGA_BASE + 0x0c);
}

static void tg_misc_write(struct pc98tridentfb *tfb, u8 value)
{
	tg_write(tfb, TG_VGA_BASE + 2, value);
	tg_select_crtc(tfb, value);
}

static u8 tg_seq_read(struct pc98tridentfb *tfb, u8 index)
{
	tg_write(tfb, TG_VGA_BASE + 4, index);
	return tg_read(tfb, TG_VGA_BASE + 5);
}

static void tg_seq_write(struct pc98tridentfb *tfb, u8 index, u8 value)
{
	tg_write(tfb, TG_VGA_BASE + 4, index);
	tg_write(tfb, TG_VGA_BASE + 5, value);
}

static u8 tg_gfx_read(struct pc98tridentfb *tfb, u8 index)
{
	tg_write(tfb, TG_VGA_BASE + 0x0e, index);
	return tg_read(tfb, TG_VGA_BASE + 0x0f);
}

static void tg_gfx_write(struct pc98tridentfb *tfb, u8 index, u8 value)
{
	tg_write(tfb, TG_VGA_BASE + 0x0e, index);
	tg_write(tfb, TG_VGA_BASE + 0x0f, value);
}

static u8 tg_crtc_read(struct pc98tridentfb *tfb, u8 index)
{
	tg_write(tfb, tfb->crtc, index);
	return tg_read(tfb, tfb->crtc + 1);
}

static void tg_crtc_write(struct pc98tridentfb *tfb, u8 index, u8 value)
{
	tg_write(tfb, tfb->crtc, index);
	tg_write(tfb, tfb->crtc + 1, value);
}

static u8 tg_attr_read(struct pc98tridentfb *tfb, u8 index)
{
	u8 value;

	tg_read(tfb, tfb->status);
	tg_write(tfb, TG_VGA_BASE, index);
	value = tg_read(tfb, TG_VGA_BASE + 1);
	tg_read(tfb, tfb->status);
	return value;
}

static void tg_attr_write(struct pc98tridentfb *tfb, u8 index, u8 value)
{
	tg_read(tfb, tfb->status);
	tg_write(tfb, TG_VGA_BASE, index);
	tg_write(tfb, TG_VGA_BASE, value);
}

static u8 tg_hidden_dac_read(struct pc98tridentfb *tfb)
{
	u8 value;

	tg_read(tfb, TG_VGA_BASE + 8);
	tg_read(tfb, TG_VGA_BASE + 6);
	tg_read(tfb, TG_VGA_BASE + 6);
	tg_read(tfb, TG_VGA_BASE + 6);
	tg_read(tfb, TG_VGA_BASE + 6);
	value = tg_read(tfb, TG_VGA_BASE + 6);
	tg_read(tfb, TG_VGA_BASE + 8);
	return value;
}

static void tg_hidden_dac_write(struct pc98tridentfb *tfb, u8 value)
{
	tg_read(tfb, TG_VGA_BASE + 8);
	tg_read(tfb, TG_VGA_BASE + 6);
	tg_read(tfb, TG_VGA_BASE + 6);
	tg_read(tfb, TG_VGA_BASE + 6);
	tg_read(tfb, TG_VGA_BASE + 6);
	tg_write(tfb, TG_VGA_BASE + 6, value);
	tg_read(tfb, TG_VGA_BASE + 8);
}

static u8 tg_sdac_read(struct pc98tridentfb *tfb, u8 index)
{
	tg_write(tfb, TG_SDAC, index);
	return tg_read(tfb, TG_SDAC - 2);
}

static void tg_sdac_write(struct pc98tridentfb *tfb, u8 index, u8 value)
{
	tg_write(tfb, TG_SDAC, index);
	tg_write(tfb, TG_SDAC - 2, value);
}

/* Reading SR0B selects new mode; writing SR0B selects old mode. */
static void tg_switch_old(struct pc98tridentfb *tfb)
{
	u8 value = tg_seq_read(tfb, 0x0b);

	tg_seq_write(tfb, 0x0b, value);
}

static void tg_switch_new(struct pc98tridentfb *tfb)
{
	tg_seq_read(tfb, 0x0b);
}

static bool tg_regs_alive(struct pc98tridentfb *tfb)
{
	u8 id, sr0, sr1;

	tg_switch_old(tfb);
	id = tg_seq_read(tfb, 0x0b);
	sr0 = tg_seq_read(tfb, 0x00);
	sr1 = tg_seq_read(tfb, 0x01);
	if (id == 0xff && sr0 == 0xff && sr1 == 0xff)
		return false;
	return id == 0xd3;
}

static int tg_claim_pio(struct pc98tridentfb *tfb)
{
	if (!request_region(0x03a0, 0x40, DRV_NAME))
		return -EBUSY;
	if (!request_region(TG_VCLK - 2, 4, DRV_NAME))
		goto err_vga;
	if (!request_region(TG_SDAC - 2, 3, DRV_NAME))
		goto err_vclk;
	tfb->pio_claimed = true;
	return 0;

err_vclk:
	release_region(TG_VCLK - 2, 4);
err_vga:
	release_region(0x03a0, 0x40);
	return -EBUSY;
}

static void tg_release_pio(struct pc98tridentfb *tfb)
{
	if (!tfb->pio_claimed)
		return;
	release_region(TG_SDAC - 2, 3);
	release_region(TG_VCLK - 2, 4);
	release_region(0x03a0, 0x40);
	tfb->pio_claimed = false;
}

static void tg_wakeup_at(void)
{
	outb(0x10, 0x46e8);
	outb(0x01, 0x0102);
	outb(0x08, 0x46e8);
}

static void tg_wakeup_pc98(void)
{
	u8 value;

	outb(0x00, 0x0094);
	outb(0x01, 0x0102);
	outb(0x20, 0x0094);
	value = inb(TG_VGA_BASE + 3);
	outb(value == 0xff ? 0x01 : value | 0x01, TG_VGA_BASE + 3);
}

static int tg_select_access_path(struct pc98tridentfb *tfb)
{
	struct pci_dev *pdev = tfb->pdev;
	int ret;

	ret = tg_claim_pio(tfb);
	if (!ret) {
		tfb->mmio = false;
		tg_select_crtc(tfb, 1);
		if (tg_regs_alive(tfb)) {
			dev_info(&pdev->dev, "using native VGA I/O registers\n");
			return 0;
		}
	}

	if (pci_resource_len(pdev, 1) >= TG_BAR1_MIN_SIZE) {
		ret = pci_request_region(pdev, 1, DRV_NAME);
		if (!ret) {
			tfb->bar1_claimed = true;
			tfb->regs_phys = pci_resource_start(pdev, 1);
			tfb->regs = pci_iomap(pdev, 1, 0);
			if (tfb->regs) {
				tfb->mmio = true;
				tg_select_crtc(tfb, 1);
				if (tg_regs_alive(tfb)) {
					tg_release_pio(tfb);
					dev_info(&pdev->dev,
						 "using BAR1 register MMIO at %pa\n",
						 &tfb->regs_phys);
					return 0;
				}
				pci_iounmap(pdev, tfb->regs);
				tfb->regs = NULL;
			}
			pci_release_region(pdev, 1);
			tfb->bar1_claimed = false;
		}
	}

	if (!tfb->pio_claimed)
		return -ENODEV;
	tfb->mmio = false;
	tg_wakeup_at();
	if (tg_regs_alive(tfb))
		return 0;
	if (allow_pc98_wakeup) {
		tg_wakeup_pc98();
		if (tg_regs_alive(tfb))
			return 0;
	}

	dev_err(&pdev->dev, "no live PIO or BAR1 register path\n");
	return -ENODEV;
}

static int tg_fingerprint(struct pc98tridentfb *tfb)
{
	u8 id, old0e, signature;

	tg_switch_old(tfb);
	id = tg_seq_read(tfb, 0x0b);
	tfb->chip_rev = tg_seq_read(tfb, 0x09);
	old0e = tg_seq_read(tfb, 0x0e);
	tg_seq_write(tfb, 0x0e, 0x00);
	signature = tg_seq_read(tfb, 0x0e);
	tg_seq_write(tfb, 0x0e, old0e ^ 0x02);

	if (id != 0xd3 || (signature & 0x0f) != 0x02) {
		dev_err(&tfb->pdev->dev,
			"fingerprint failed: SR0B=%02x SR09=%02x SR0E=%02x\n",
			id, tfb->chip_rev, signature);
		return -ENODEV;
	}
	dev_info(&tfb->pdev->dev,
		 "TGUI96xx fingerprint: SR0B=%02x SR09=%02x\n",
		 id, tfb->chip_rev);
	return 0;
}

static resource_size_t tg_vram_size(struct pc98tridentfb *tfb)
{
	u8 value;

	tg_switch_new(tfb);
	tg_select_crtc(tfb, tg_misc_read(tfb));
	value = tg_crtc_read(tfb, 0x1f) & 0x0f;
	switch (value) {
	case 0x01:
		return SZ_512K;
	case 0x03:
		return SZ_1M;
	case 0x07:
		return SZ_2M;
	case 0x0f:
		return SZ_4M;
	default:
		dev_warn(&tfb->pdev->dev,
			 "unknown CR1F VRAM code %02x; assuming 2 MiB\n",
			 value);
		return SZ_2M;
	}
}

static int tg_map_vram(struct pc98tridentfb *tfb)
{
	struct pci_dev *pdev = tfb->pdev;
	resource_size_t bar0 = pci_resource_start(pdev, 0);
	resource_size_t decoded;
	u8 cr21 = tg_crtc_read(tfb, 0x21);
	int ret;

	decoded = ((resource_size_t)(cr21 & 0x0f) << 28) |
		  ((resource_size_t)((cr21 >> 6) & 0x03) << 24);
	if (fb_phys)
		tfb->fb_phys = fb_phys;
	else if (tfb->mmio)
		tfb->fb_phys = decoded ? decoded : 0x73000000;
	else
		tfb->fb_phys = bar0;

	if (tfb->fb_phys == bar0 &&
	    pci_resource_len(pdev, 0) >= tfb->vram_size) {
		ret = pci_request_region(pdev, 0, DRV_NAME);
		if (ret)
			return ret;
		tfb->bar0_claimed = true;
		tfb->vram = pci_iomap_range(pdev, 0, 0, tfb->vram_size);
	} else {
		if (!request_mem_region(tfb->fb_phys, tfb->vram_size,
					DRV_NAME))
			return -EBUSY;
		tfb->fixed_fb_claimed = true;
		tfb->vram = ioremap(tfb->fb_phys, tfb->vram_size);
	}
	if (!tfb->vram)
		goto err_release_region;

	dev_info(&pdev->dev, "framebuffer at %pa, %pa bytes (CR21=%02x)\n",
		 &tfb->fb_phys, &tfb->vram_size, cr21);
	return 0;

err_release_region:
	if (tfb->bar0_claimed) {
		pci_release_region(pdev, 0);
		tfb->bar0_claimed = false;
	}
	if (tfb->fixed_fb_claimed) {
		release_mem_region(tfb->fb_phys, tfb->vram_size);
		tfb->fixed_fb_claimed = false;
	}
	return -ENOMEM;
}

static void tg_unmap_vram(struct pc98tridentfb *tfb)
{
	if (tfb->vram) {
		if (tfb->bar0_claimed)
			pci_iounmap(tfb->pdev, tfb->vram);
		else
			iounmap(tfb->vram);
		tfb->vram = NULL;
	}
	if (tfb->bar0_claimed) {
		pci_release_region(tfb->pdev, 0);
		tfb->bar0_claimed = false;
	}
	if (tfb->fixed_fb_claimed) {
		release_mem_region(tfb->fb_phys, tfb->vram_size);
		tfb->fixed_fb_claimed = false;
	}
}

static void tg_save_state(struct pc98tridentfb *tfb)
{
	struct pc98trident_saved *sv = &tfb->saved;
	int i;

	sv->misc = tg_misc_read(tfb);
	tg_select_crtc(tfb, sv->misc);
	tg_switch_new(tfb);
	sv->sr0d_new = tg_seq_read(tfb, 0x0d);
	sv->sr0e_new = tg_seq_read(tfb, 0x0e);
	tg_switch_old(tfb);
	sv->sr0d_old = tg_seq_read(tfb, 0x0d);
	tg_switch_new(tfb);
	for (i = 0; i < ARRAY_SIZE(sv->sr); i++)
		if (i != 0x0b && i != 0x0d && i != 0x0e)
			sv->sr[i] = tg_seq_read(tfb, i);
	for (i = 0; i < ARRAY_SIZE(sv->crtc); i++)
		sv->crtc[i] = tg_crtc_read(tfb, i);
	for (i = 0; i < ARRAY_SIZE(sv->gfx); i++)
		sv->gfx[i] = tg_gfx_read(tfb, i);
	for (i = 0; i < ARRAY_SIZE(sv->attr); i++)
		sv->attr[i] = tg_attr_read(tfb, i);
	sv->hidden_dac = tg_hidden_dac_read(tfb);
	sv->dac_mask = tg_read(tfb, TG_VGA_BASE + 6);
	tg_write(tfb, TG_VGA_BASE + 7, 0);
	for (i = 0; i < ARRAY_SIZE(sv->dac); i++)
		sv->dac[i] = tg_read(tfb, TG_VGA_BASE + 9);
	sv->vclk_lo = tg_read(tfb, TG_VCLK);
	sv->vclk_hi = tg_read(tfb, TG_VCLK + 1);
	sv->sdac04 = tg_sdac_read(tfb, 0x04);
	sv->valid = true;
}

static void tg_restore_state(struct pc98tridentfb *tfb)
{
	struct pc98trident_saved *sv = &tfb->saved;
	int i;

	if (!sv->valid)
		return;
	tg_switch_new(tfb);
	for (i = 0; i < ARRAY_SIZE(sv->sr); i++) {
		if (i == 0x01 || i == 0x0b || i == 0x0d || i == 0x0e)
			continue;
		tg_seq_write(tfb, i, sv->sr[i]);
	}
	tg_switch_old(tfb);
	tg_seq_write(tfb, 0x0d, sv->sr0d_old);
	tg_switch_new(tfb);
	tg_seq_write(tfb, 0x0d, sv->sr0d_new);
	tg_misc_write(tfb, sv->misc);
	tg_crtc_write(tfb, 0x11, sv->crtc[0x11] & 0x7f);
	for (i = 0; i < ARRAY_SIZE(sv->crtc); i++)
		if (i != 0x11)
			tg_crtc_write(tfb, i, sv->crtc[i]);
	tg_crtc_write(tfb, 0x11, sv->crtc[0x11]);
	for (i = 0; i < ARRAY_SIZE(sv->gfx); i++)
		tg_gfx_write(tfb, i, sv->gfx[i]);
	for (i = 0; i < ARRAY_SIZE(sv->attr); i++)
		tg_attr_write(tfb, i, sv->attr[i]);
	tg_read(tfb, tfb->status);
	tg_write(tfb, TG_VGA_BASE, 0x20);
	tg_write(tfb, TG_VGA_BASE + 8, 0);
	for (i = 0; i < ARRAY_SIZE(sv->dac); i++)
		tg_write(tfb, TG_VGA_BASE + 9, sv->dac[i]);
	tg_hidden_dac_write(tfb, sv->hidden_dac);
	tg_write(tfb, TG_VGA_BASE + 6, sv->dac_mask);
	tg_write(tfb, TG_VCLK, sv->vclk_lo);
	tg_write(tfb, TG_VCLK + 1, sv->vclk_hi);
	tg_sdac_write(tfb, 0x04, sv->sdac04);
	tg_seq_write(tfb, 0x0e, sv->sr0e_new ^ 0x02);
	tg_seq_write(tfb, 0x01, sv->sr[0x01]);
	sv->valid = false;
}

static const u8 tg_crtc_640x480[] = {
	0x5f, 0x4f, 0x50, 0x02, 0x52, 0x9e, 0x0b, 0x3e,
	0x00, 0x40, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00,
	0xea, 0x0c, 0xdf, 0x00, 0x00, 0xe0, 0x0b, 0xc3,
	0xff,
};

static void tg_load_palette(struct pc98tridentfb *tfb)
{
	unsigned int i;

	tg_write(tfb, TG_VGA_BASE + 6, 0xff);
	tg_write(tfb, TG_VGA_BASE + 8, 0);
	for (i = 0; i < 256; i++) {
		u8 r = (i >> 5) & 7;
		u8 g = (i >> 2) & 7;
		u8 b = i & 3;

		tg_write(tfb, TG_VGA_BASE + 9, r * 63 / 7);
		tg_write(tfb, TG_VGA_BASE + 9, g * 63 / 7);
		tg_write(tfb, TG_VGA_BASE + 9, b * 63 / 3);
	}
}

static void tg_set_mode(struct pc98tridentfb *tfb)
{
	unsigned int i;
	u16 offset = TG_PITCH / 8;

	tg_switch_new(tfb);
	tg_seq_write(tfb, 0x00, 0x03);
	tg_seq_write(tfb, 0x01, 0x21);
	tg_seq_write(tfb, 0x0e, 0x82);
	tg_seq_write(tfb, 0x02, 0x0f);
	tg_seq_write(tfb, 0x03, 0x00);
	tg_seq_write(tfb, 0x04, 0x0e);
	tg_switch_old(tfb);
	tg_seq_write(tfb, 0x0d, 0x20);
	tg_switch_new(tfb);
	tg_seq_write(tfb, 0x0d, 0x00);

	tg_write(tfb, TG_VCLK, 0xd7);
	tg_write(tfb, TG_VCLK + 1, 0x1c);
	tg_misc_write(tfb, 0xeb);
	tg_crtc_write(tfb, 0x11, tg_crtc_read(tfb, 0x11) & 0x7f);
	for (i = 0; i < ARRAY_SIZE(tg_crtc_640x480); i++)
		tg_crtc_write(tfb, i,
			      i == 0x13 ? offset : tg_crtc_640x480[i]);
	tg_crtc_write(tfb, 0x27, (tg_crtc_read(tfb, 0x27) & 0x07) | 0x08);
	tg_crtc_write(tfb, 0x2b, 0);
	tg_crtc_write(tfb, 0x21, tg_crtc_read(tfb, 0x21) | 0x20);
	tg_crtc_write(tfb, 0x29, (tg_crtc_read(tfb, 0x29) & 0xcf) |
			      ((offset & 0x300) >> 4));
	tg_crtc_write(tfb, 0x38, 0x00);
	if (tfb->mmio)
		tg_crtc_write(tfb, 0x39,
			      (tg_crtc_read(tfb, 0x39) & ~0x06) | 0x01);
	else
		tg_crtc_write(tfb, 0x39, tg_crtc_read(tfb, 0x39) & ~0x07);
	tg_crtc_write(tfb, 0x50, 0);

	for (i = 0; i <= 4; i++)
		tg_gfx_write(tfb, i, 0);
	tg_gfx_write(tfb, 0x05, 0x40);
	tg_gfx_write(tfb, 0x06, 0x05);
	tg_gfx_write(tfb, 0x07, 0x0f);
	tg_gfx_write(tfb, 0x08, 0xff);

	tg_read(tfb, tfb->status);
	for (i = 0; i < 16; i++) {
		tg_write(tfb, TG_VGA_BASE, i);
		tg_write(tfb, TG_VGA_BASE, i);
	}
	tg_attr_write(tfb, 0x10, 0x41);
	tg_attr_write(tfb, 0x11, 0x00);
	tg_attr_write(tfb, 0x12, 0x0f);
	tg_attr_write(tfb, 0x13, 0x00);
	tg_attr_write(tfb, 0x14, 0x00);
	tg_read(tfb, tfb->status);
	tg_write(tfb, TG_VGA_BASE, 0x20);
	tg_hidden_dac_write(tfb, 0x00);
	tg_load_palette(tfb);

	/* Ra43 field-proven fetch state: XF98 minus CR2A bit 6. */
	tg_crtc_write(tfb, 0x1e, 0x80);
	tg_crtc_write(tfb, 0x2a, tfb->saved.crtc[0x2a]);
	tg_crtc_write(tfb, 0x2f, tfb->saved.crtc[0x2f] | 0x10);
	tg_gfx_write(tfb, 0x0f, (tfb->saved.gfx[0x0f] & 0xf0) | 0x12);
	tg_gfx_write(tfb, 0x2f, 0x24);
}

static void tg_relay_to_trident(struct pc98tridentfb *tfb)
{
	u8 value;

	outb(0x0e, PC98_GDC_MODE);
	inb(PC98_WAIT);
	inb(PC98_WAIT);
	tg_gfx_write(tfb, 0x21, tg_gfx_read(tfb, 0x21) & ~0x20);
	tg_crtc_write(tfb, 0x23, tg_crtc_read(tfb, 0x23) & ~0x20);
	tg_crtc_write(tfb, 0x29, tg_crtc_read(tfb, 0x29) | 0x04);
	value = tg_sdac_read(tfb, 0x04) | 0x06;
	tg_sdac_write(tfb, 0x04, value);
	usleep_range(1000, 2000);
	tg_sdac_write(tfb, 0x04, value | 0x08);
	tg_gfx_write(tfb, 0x23, tg_gfx_read(tfb, 0x23) & ~0x03);
	tg_sdac_write(tfb, 0x04, value | 0x09);
	tg_seq_write(tfb, 0x01, tg_seq_read(tfb, 0x01) & ~0x10);
	outb(0x02, PC98_RELAY);
	tfb->relay_active = true;
}

static void tg_relay_to_gdc(struct pc98tridentfb *tfb)
{
	if (!tfb->relay_active)
		return;
	outb(0x00, PC98_RELAY);
	tg_seq_write(tfb, 0x01, tg_seq_read(tfb, 0x01) | 0x10);
	tg_sdac_write(tfb, 0x04, tg_sdac_read(tfb, 0x04) & ~0x0f);
	tg_gfx_write(tfb, 0x23, (tg_gfx_read(tfb, 0x23) & ~0x03) | 0x01);
	tg_crtc_write(tfb, 0x29, tg_crtc_read(tfb, 0x29) & ~0x04);
	tg_crtc_write(tfb, 0x23, tg_crtc_read(tfb, 0x23) | 0x20);
	tg_gfx_write(tfb, 0x21, tg_gfx_read(tfb, 0x21) | 0x20);
	outb(0x0f, PC98_GDC_MODE);
	tfb->relay_active = false;
}

static int pc98tridentfb_check_var(struct fb_var_screeninfo *var,
				   struct fb_info *info)
{
	if (var->xres != TG_WIDTH || var->yres != TG_HEIGHT ||
	    var->bits_per_pixel != TG_BPP)
		return -EINVAL;
	var->xres_virtual = TG_WIDTH;
	var->yres_virtual = TG_HEIGHT;
	var->red.offset = 5;
	var->red.length = 3;
	var->green.offset = 2;
	var->green.length = 3;
	var->blue.offset = 0;
	var->blue.length = 2;
	var->transp.length = 0;
	return 0;
}

static int pc98tridentfb_setcolreg(unsigned int regno, unsigned int red,
				   unsigned int green, unsigned int blue,
				   unsigned int transp,
				   struct fb_info *info)
{
	struct pc98tridentfb *tfb = info->par;
	unsigned long flags;

	if (regno >= 256)
		return -EINVAL;
	spin_lock_irqsave(&tfb->reg_lock, flags);
	tg_write(tfb, TG_VGA_BASE + 8, regno);
	tg_write(tfb, TG_VGA_BASE + 9, red >> 10);
	tg_write(tfb, TG_VGA_BASE + 9, green >> 10);
	tg_write(tfb, TG_VGA_BASE + 9, blue >> 10);
	spin_unlock_irqrestore(&tfb->reg_lock, flags);
	return 0;
}

static int pc98tridentfb_blank(int blank, struct fb_info *info)
{
	struct pc98tridentfb *tfb = info->par;
	unsigned long flags;
	u8 value;

	spin_lock_irqsave(&tfb->reg_lock, flags);
	value = tg_seq_read(tfb, 0x01);
	if (blank == FB_BLANK_UNBLANK)
		value &= ~0x20;
	else
		value |= 0x20;
	tg_seq_write(tfb, 0x01, value);
	spin_unlock_irqrestore(&tfb->reg_lock, flags);
	return 0;
}

/*
 * Ra43 field testing found that the linear aperture can drop complete dword
 * writes when a CPU emits a long stream while scanout is active.  Reads are
 * reliable.  Pace every four writes with a read, then repair any missing
 * dwords.  This is the same fallback used by StratoHAL's 98disp_trident.c.
 */
static int tg_store_verified(struct pc98tridentfb *tfb, unsigned long offset,
			     const u8 *src, size_t nbytes)
{
	u8 __iomem *dst = tfb->vram + offset;
	const u32 *src32 = (const u32 *)src;
	size_t ndwords = nbytes / sizeof(*src32);
	unsigned int pass;
	size_t i;
	bool bad;

	if (WARN_ON_ONCE((offset | nbytes) & (sizeof(*src32) - 1)))
		return -EINVAL;

	for (i = 0; i < ndwords; i++) {
		writel(src32[i], dst + i * sizeof(*src32));
		if ((i & 3) == 3)
			(void)readl(dst + i * sizeof(*src32));
	}
	if (ndwords && (ndwords & 3))
		(void)readl(dst + (ndwords - 1) * sizeof(*src32));

	for (pass = 0; pass < TG_VERIFY_PASSES; pass++) {
		bad = false;
		for (i = 0; i < ndwords; i++) {
			if (readl(dst + i * sizeof(*src32)) == src32[i])
				continue;
			writel(src32[i], dst + i * sizeof(*src32));
			(void)readl(dst + i * sizeof(*src32));
			bad = true;
		}
		if (!bad)
			return pass;
	}

	return -EIO;
}

static void tg_flush_rows(struct fb_info *info, unsigned int first,
			  unsigned int last)
{
	struct pc98tridentfb *tfb = info->par;
	unsigned int failed = 0;
	unsigned int y;

	first = min(first, (unsigned int)TG_HEIGHT);
	last = min(last, (unsigned int)TG_HEIGHT);
	if (first >= last)
		return;

	mutex_lock(&tfb->vram_lock);
	for (y = first; y < last; y++) {
		unsigned long offset = y * TG_PITCH;

		if (tg_store_verified(tfb, offset, tfb->shadow + offset,
				      TG_PITCH) < 0)
			failed++;
	}
	mutex_unlock(&tfb->vram_lock);

	if (failed)
		dev_warn_ratelimited(&tfb->pdev->dev,
				     "VRAM verification failed on %u row(s)\n",
				     failed);
}

static void pc98tridentfb_damage_range(struct fb_info *info, off_t offset,
				       size_t len)
{
	size_t end;

	if (!len || offset < 0 || offset >= TG_FB_SIZE)
		return;
	end = offset + min_t(size_t, len, TG_FB_SIZE - offset);
	tg_flush_rows(info, offset / TG_PITCH,
		      DIV_ROUND_UP(end, TG_PITCH));
}

static void pc98tridentfb_damage_area(struct fb_info *info, u32 x, u32 y,
				      u32 width, u32 height)
{
	if (!width || !height || x >= TG_WIDTH || y >= TG_HEIGHT)
		return;
	tg_flush_rows(info, y, y + min_t(u32, height, TG_HEIGHT - y));
}

static void pc98tridentfb_deferred_io(struct fb_info *info,
				      struct list_head *pagelist)
{
	struct fb_deferred_io_pageref *pageref;
	size_t first = TG_FB_SIZE;
	size_t last = 0;

	list_for_each_entry(pageref, pagelist, list) {
		size_t offset = min_t(size_t, pageref->offset, TG_FB_SIZE);
		size_t end = min_t(size_t, offset + PAGE_SIZE, TG_FB_SIZE);

		first = min(first, offset);
		last = max(last, end);
	}
	if (first < last)
		tg_flush_rows(info, first / TG_PITCH,
			      DIV_ROUND_UP(last, TG_PITCH));
}

FB_GEN_DEFAULT_DEFERRED_SYSMEM_OPS(pc98tridentfb,
				   pc98tridentfb_damage_range,
				   pc98tridentfb_damage_area)

static const struct fb_ops pc98tridentfb_ops = {
	.owner		= THIS_MODULE,
	FB_DEFAULT_DEFERRED_OPS(pc98tridentfb),
	.fb_check_var	= pc98tridentfb_check_var,
	.fb_setcolreg	= pc98tridentfb_setcolreg,
	.fb_blank	= pc98tridentfb_blank,
};

static struct fb_deferred_io pc98tridentfb_defio = {
	.delay			= DIV_ROUND_UP(HZ, 60),
	.sort_pagereflist	= true,
	.deferred_io		= pc98tridentfb_deferred_io,
};

static void tg_release_access_path(struct pc98tridentfb *tfb)
{
	if (tfb->regs)
		pci_iounmap(tfb->pdev, tfb->regs);
	if (tfb->bar1_claimed)
		pci_release_region(tfb->pdev, 1);
	tfb->regs = NULL;
	tfb->bar1_claimed = false;
	tg_release_pio(tfb);
}

static int pc98tridentfb_probe(struct pci_dev *pdev,
			       const struct pci_device_id *id)
{
	struct pc98tridentfb *tfb;
	struct fb_info *info;
	int ret;

	ret = pci_enable_device(pdev);
	if (ret)
		return ret;

	info = framebuffer_alloc(sizeof(*tfb), &pdev->dev);
	if (!info) {
		ret = -ENOMEM;
		goto err_disable;
	}
	tfb = info->par;
	tfb->pdev = pdev;
	tfb->info = info;
	tfb->crtc = TG_CRTC_COLOR;
	tfb->status = TG_STATUS_COLOR;
	spin_lock_init(&tfb->reg_lock);
	mutex_init(&tfb->vram_lock);

	ret = tg_select_access_path(tfb);
	if (ret)
		goto err_release_info;
	ret = tg_fingerprint(tfb);
	if (ret)
		goto err_access;
	tfb->vram_size = tg_vram_size(tfb);
	tg_save_state(tfb);
	ret = tg_map_vram(tfb);
	if (ret)
		goto err_restore;
	tfb->shadow = vzalloc(TG_FB_SIZE);
	if (!tfb->shadow) {
		ret = -ENOMEM;
		goto err_unwind_video;
	}

	tg_set_mode(tfb);
	tg_relay_to_trident(tfb);
	tg_flush_rows(info, 0, TG_HEIGHT);
	tg_seq_write(tfb, 0x01, 0x01);

	strscpy(info->fix.id, "PC98 TGUI96xx", sizeof(info->fix.id));
	info->fix.smem_start = 0;
	info->fix.smem_len = TG_FB_SIZE;
	info->fix.type = FB_TYPE_PACKED_PIXELS;
	info->fix.visual = FB_VISUAL_PSEUDOCOLOR;
	info->fix.line_length = TG_PITCH;
	info->fix.accel = FB_ACCEL_NONE;
	info->var.xres = TG_WIDTH;
	info->var.yres = TG_HEIGHT;
	info->var.xres_virtual = TG_WIDTH;
	info->var.yres_virtual = TG_HEIGHT;
	info->var.bits_per_pixel = TG_BPP;
	info->var.activate = FB_ACTIVATE_NOW;
	info->var.height = -1;
	info->var.width = -1;
	pc98tridentfb_check_var(&info->var, info);
	info->screen_buffer = tfb->shadow;
	info->screen_size = TG_FB_SIZE;
	info->fbops = &pc98tridentfb_ops;
	info->flags = FBINFO_VIRTFB | FBINFO_HWACCEL_DISABLED;
	info->fbdefio = &pc98tridentfb_defio;

	ret = fb_alloc_cmap(&info->cmap, 256, 0);
	if (ret)
		goto err_unwind_video;
	ret = fb_deferred_io_init(info);
	if (ret)
		goto err_cmap;
	ret = register_framebuffer(info);
	if (ret)
		goto err_defio;

	pci_set_drvdata(pdev, info);
	dev_info(&pdev->dev,
		 "fb%d: PC-98 TGUI96xx 640x480x8, pitch %u, verified shadow writes\n",
		 info->node, TG_PITCH);
	return 0;

err_defio:
	fb_deferred_io_cleanup(info);
err_cmap:
	fb_dealloc_cmap(&info->cmap);
err_unwind_video:
	tg_relay_to_gdc(tfb);
	tg_restore_state(tfb);
	vfree(tfb->shadow);
	tfb->shadow = NULL;
	tg_unmap_vram(tfb);
	goto err_access;
err_restore:
	tg_restore_state(tfb);
err_access:
	tg_release_access_path(tfb);
err_release_info:
	framebuffer_release(info);
err_disable:
	pci_disable_device(pdev);
	return ret;
}

static void pc98tridentfb_remove(struct pci_dev *pdev)
{
	struct fb_info *info = pci_get_drvdata(pdev);
	struct pc98tridentfb *tfb;

	if (!info)
		return;
	tfb = info->par;
	unregister_framebuffer(info);
	fb_deferred_io_cleanup(info);
	fb_dealloc_cmap(&info->cmap);
	tg_seq_write(tfb, 0x01, tg_seq_read(tfb, 0x01) | 0x20);
	tg_relay_to_gdc(tfb);
	tg_restore_state(tfb);
	vfree(tfb->shadow);
	tfb->shadow = NULL;
	tg_unmap_vram(tfb);
	tg_release_access_path(tfb);
	framebuffer_release(info);
	pci_disable_device(pdev);
}

static const struct pci_device_id pc98tridentfb_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_TRIDENT, PCI_DEVICE_TGUI9660) },
	{ }
};

static struct pci_driver pc98tridentfb_driver = {
	.name		= DRV_NAME,
	.id_table	= pc98tridentfb_ids,
	.probe		= pc98tridentfb_probe,
	.remove		= pc98tridentfb_remove,
};
module_pci_driver(pc98tridentfb_driver);

MODULE_DESCRIPTION("NEC PC-9821 built-in Trident TGUI96xx framebuffer");
MODULE_AUTHOR("Awe Morris, Keiichi Tabata");
MODULE_LICENSE("GPL");
