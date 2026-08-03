// SPDX-License-Identifier: GPL-2.0-only
/*
 * NEC PC-9800 native bus mouse driver.
 *
 * The mouse is attached to a uPD8255 PPI at 0x7fd9-0x7fdf and raises IRQ13.
 * This is a Linux input driver, so the same device is available through
 * evdev, mousedev, gpm and Xorg rather than through an X-only interface.
 *
 * Based on the Linux 2.6.7 PC-98 driver by Osamu Tomita.
 *
 * Copyright (c) 2002 Osamu Tomita
 * Copyright (C) 2026 Awe Morris
 *
 * The historical driver was based on work by James Banks, Matthew Dillon,
 * David Giller, Nathan Laredo, Linus Torvalds, Johan Myreen, Cliff Matthews,
 * Philip Blundell, Russell King, and Vojtech Pavlik.
 */

#include <linux/init.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/module.h>

#define PC98BM_DATA_PORT	0x7fd9
#define PC98BM_CONTROL_PORT	0x7fdd
#define PC98BM_CONFIG_PORT	0x7fdf
#define PC98BM_TIMER_PORT	0xbfdb

#define PC98BM_ENABLE_IRQ	0x00
#define PC98BM_DISABLE_IRQ	0x10
#define PC98BM_READ_X_LOW	0x80
#define PC98BM_READ_X_HIGH	0xa0
#define PC98BM_READ_Y_LOW	0xc0
#define PC98BM_READ_Y_HIGH	0xe0

#define PC98BM_DEFAULT_MODE	0x93
#define PC98BM_DEFAULT_TIMER	0x00
#define PC98BM_DEFAULT_IRQ	13

static unsigned int irq = PC98BM_DEFAULT_IRQ;
module_param(irq, uint, 0444);
MODULE_PARM_DESC(irq, "IRQ number (13 by default)");

static const unsigned short pc98bm_ports[] = {
	0x7fd9, 0x7fdb, 0x7fdd, 0x7fdf, PC98BM_TIMER_PORT,
};

struct pc98bm {
	struct input_dev *input;
};

static struct pc98bm pc98bm;

static irqreturn_t pc98bm_interrupt(int irqno, void *dev_id)
{
	struct pc98bm *mouse = dev_id;
	u8 buttons;
	s8 dx, dy;

	/* HC 0->1 latches both signed counters; SHL/SXY select each nibble. */
	outb(PC98BM_READ_X_LOW, PC98BM_CONTROL_PORT);
	dx = inb(PC98BM_DATA_PORT) & 0x0f;
	outb(PC98BM_READ_X_HIGH, PC98BM_CONTROL_PORT);
	dx |= (inb(PC98BM_DATA_PORT) & 0x0f) << 4;
	outb(PC98BM_READ_Y_LOW, PC98BM_CONTROL_PORT);
	dy = inb(PC98BM_DATA_PORT) & 0x0f;
	outb(PC98BM_READ_Y_HIGH, PC98BM_CONTROL_PORT);
	buttons = inb(PC98BM_DATA_PORT);
	dy |= (buttons & 0x0f) << 4;
	buttons = (~buttons >> 5) & 0x07;

	input_report_rel(mouse->input, REL_X, dx);
	input_report_rel(mouse->input, REL_Y, dy);
	input_report_key(mouse->input, BTN_RIGHT, !!(buttons & BIT(0)));
	input_report_key(mouse->input, BTN_MIDDLE, !!(buttons & BIT(1)));
	input_report_key(mouse->input, BTN_LEFT, !!(buttons & BIT(2)));
	input_sync(mouse->input);

	/* Drops HC and unmasks the next periodic IRQ. */
	outb(PC98BM_ENABLE_IRQ, PC98BM_CONTROL_PORT);
	return IRQ_HANDLED;
}

static int pc98bm_open(struct input_dev *input)
{
	int error;

	error = request_irq(irq, pc98bm_interrupt, 0, "pc98busmouse", &pc98bm);
	if (error) {
		pr_err("pc98busmouse: cannot request IRQ %u: %d\n", irq, error);
		return error;
	}

	outb(PC98BM_ENABLE_IRQ, PC98BM_CONTROL_PORT);
	return 0;
}

static void pc98bm_close(struct input_dev *input)
{
	outb(PC98BM_DISABLE_IRQ, PC98BM_CONTROL_PORT);
	free_irq(irq, &pc98bm);
}

static void pc98bm_release_ports(void)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(pc98bm_ports); i++)
		release_region(pc98bm_ports[i], 1);
}

static int pc98bm_request_ports(void)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(pc98bm_ports); i++) {
		if (!request_region(pc98bm_ports[i], 1, "pc98busmouse")) {
			pr_err("pc98busmouse: I/O port %#x is busy\n",
			       pc98bm_ports[i]);
			while (i > 0) {
				i--;
				release_region(pc98bm_ports[i], 1);
			}
			return -EBUSY;
		}
	}
	return 0;
}

static int __init pc98bm_init(void)
{
	struct input_dev *input;
	int error;

	error = pc98bm_request_ports();
	if (error)
		return error;

	input = input_allocate_device();
	if (!input) {
		error = -ENOMEM;
		goto err_ports;
	}

	pc98bm.input = input;
	input->name = "PC-9800 bus mouse";
	input->phys = "isa7fd9/input0";
	input->id.bustype = BUS_ISA;
	input->id.vendor = 0x0004;
	input->id.product = 0x0001;
	input->id.version = 0x0100;
	input->open = pc98bm_open;
	input->close = pc98bm_close;

	input_set_capability(input, EV_REL, REL_X);
	input_set_capability(input, EV_REL, REL_Y);
	input_set_capability(input, EV_KEY, BTN_LEFT);
	input_set_capability(input, EV_KEY, BTN_MIDDLE);
	input_set_capability(input, EV_KEY, BTN_RIGHT);

	outb(PC98BM_DEFAULT_MODE, PC98BM_CONFIG_PORT);
	outb(PC98BM_DISABLE_IRQ, PC98BM_CONTROL_PORT);
	outb(PC98BM_DEFAULT_TIMER, PC98BM_TIMER_PORT);

	error = input_register_device(input);
	if (error)
		goto err_input;

	pr_info("input: PC-9800 bus mouse at 0x7fd9, IRQ %u\n", irq);
	return 0;

err_input:
	input_free_device(input);
err_ports:
	pc98bm_release_ports();
	return error;
}

static void __exit pc98bm_exit(void)
{
	input_unregister_device(pc98bm.input);
	pc98bm_release_ports();
}

module_init(pc98bm_init);
module_exit(pc98bm_exit);

MODULE_AUTHOR("Osamu Tomita, Awe Morris, Keiichi Tabata");
MODULE_DESCRIPTION("NEC PC-9800 native bus mouse driver");
MODULE_LICENSE("GPL");
