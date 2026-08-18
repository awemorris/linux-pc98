// SPDX-License-Identifier: GPL-2.0
/*
 * Keyboard driver for the NEC PC-9800.
 *
 * The uPD8251 reset sequence is adapted from the NetBSD/pc98 keyboard
 * driver.  Its original copyright and license notice is retained below.
 *
 * Copyright (c) 1994, 1995, 1996, 1997, 1998
 *     NetBSD/pc98 porting staff. All rights reserved.
 * Copyright (c) 1994, 1995, 1996, 1997, 1998, 1999
 *     Naofumi HONDA. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Copyright (C) 2026 Awe Morris
 *
 * The keyboard is a uPD8251 USART: data at I/O 0x41, status/command at 0x43,
 * and it raises IRQ1 (master PIC IR1) when a scancode arrives. Each byte is a
 * PC-98 scancode; bit7 set means key release (break), clear means press (make).
 *
 * Scancodes are received on IRQ1 and mapped to Linux keycodes; the VT keyboard
 * layer then applies the loaded keymap, so a US keymap gives the expected
 * ASCII for the letter, digit, space, enter and backspace keys. Some JIS-only
 * symbol keys are mapped approximately.
 */
#include <linux/input.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/timer.h>

#define PC98KBD_DATA	0x41	/* R: scancode  W: mode / ctrl-send */
#define PC98KBD_CMD	0x43	/* R: status    W: command */
#define PC98KBD_ST_RXRDY 0x02	/* status bit: a scancode is ready */
#define PC98KBD_ST_ERROR	0x38	/* parity, overrun or framing error */
#define PC98KBD_IRQ	1

#define PC98KBD_IO_DELAY_US	20
#define PC98KBD_RECOVERY_MS	100
#define PC98KBD_DRAIN_LIMIT	256

static void pc98kbd_ctl_write(u8 value)
{
	outb(value, PC98KBD_CMD);
	udelay(PC98KBD_IO_DELAY_US);
}

static void pc98kbd_hw_init(void)
{
	/*
	 * Reset the uPD8251 from an unknown firmware state.  The 0x5e mode
	 * byte selects the PC-98 keyboard's 8-bit, odd-parity, one-stop-bit
	 * asynchronous format at x16.  Keep the recovery delays used by the
	 * NetBSD/pc98 driver; real hardware cannot accept back-to-back writes
	 * as quickly as QEMU can.
	 */
	pc98kbd_ctl_write(0x00);
	pc98kbd_ctl_write(0x00);
	pc98kbd_ctl_write(0x00);
	pc98kbd_ctl_write(0x40);
	pc98kbd_ctl_write(0x5e);
	pc98kbd_ctl_write(0x3a);
	pc98kbd_ctl_write(0x32);
	pc98kbd_ctl_write(0x16);

	/*
	 * Reset the keyboard-side protocol after the USART has settled.  This
	 * is the sequence used by NetBSD/pc98: temporarily enable transmit,
	 * send a zero command, then return to receive-only operation.
	 */
	mdelay(5);
	if (inb(PC98KBD_CMD) & PC98KBD_ST_RXRDY)
		inb(PC98KBD_DATA);
	pc98kbd_ctl_write(0x17);
	outb(0x00, PC98KBD_DATA);
	udelay(PC98KBD_IO_DELAY_US);
	pc98kbd_ctl_write(0x16);
	mdelay(50);
}

static const unsigned short pc98kbd_keycode[128] = {
	[0x00] = KEY_ESC,
	[0x01] = KEY_1,		[0x02] = KEY_2,		[0x03] = KEY_3,
	[0x04] = KEY_4,		[0x05] = KEY_5,		[0x06] = KEY_6,
	[0x07] = KEY_7,		[0x08] = KEY_8,		[0x09] = KEY_9,
	[0x0a] = KEY_0,		[0x0b] = KEY_MINUS,	[0x0c] = KEY_EQUAL,
	[0x0d] = KEY_BACKSLASH,	[0x0e] = KEY_BACKSPACE,	[0x0f] = KEY_TAB,
	[0x10] = KEY_Q,		[0x11] = KEY_W,		[0x12] = KEY_E,
	[0x13] = KEY_R,		[0x14] = KEY_T,		[0x15] = KEY_Y,
	[0x16] = KEY_U,		[0x17] = KEY_I,		[0x18] = KEY_O,
	[0x19] = KEY_P,		[0x1a] = KEY_LEFTBRACE,	[0x1b] = KEY_RIGHTBRACE,
	[0x1c] = KEY_ENTER,	[0x1d] = KEY_A,		[0x1e] = KEY_S,
	[0x1f] = KEY_D,		[0x20] = KEY_F,		[0x21] = KEY_G,
	[0x22] = KEY_H,		[0x23] = KEY_J,		[0x24] = KEY_K,
	[0x25] = KEY_L,		[0x26] = KEY_SEMICOLON,	[0x27] = KEY_APOSTROPHE,
	[0x28] = KEY_GRAVE,	[0x29] = KEY_Z,		[0x2a] = KEY_X,
	[0x2b] = KEY_C,		[0x2c] = KEY_V,		[0x2d] = KEY_B,
	[0x2e] = KEY_N,		[0x2f] = KEY_M,		[0x30] = KEY_COMMA,
	[0x31] = KEY_DOT,	[0x32] = KEY_SLASH,	[0x33] = KEY_RO,
	[0x34] = KEY_SPACE,	[0x35] = KEY_HENKAN,	[0x36] = KEY_PAGEUP,
	[0x37] = KEY_PAGEDOWN,	[0x38] = KEY_INSERT,	[0x39] = KEY_DELETE,
	[0x3a] = KEY_UP,	[0x3b] = KEY_LEFT,	[0x3c] = KEY_RIGHT,
	[0x3d] = KEY_DOWN,	[0x3e] = KEY_HOME,	[0x3f] = KEY_END,
	[0x40] = KEY_KPMINUS,	[0x41] = KEY_KPSLASH,	[0x42] = KEY_KP7,
	[0x43] = KEY_KP8,	[0x44] = KEY_KP9,	[0x45] = KEY_KPASTERISK,
	[0x46] = KEY_KP4,	[0x47] = KEY_KP5,	[0x48] = KEY_KP6,
	[0x49] = KEY_KPPLUS,	[0x4a] = KEY_KP1,	[0x4b] = KEY_KP2,
	[0x4c] = KEY_KP3,	[0x4d] = KEY_KPEQUAL,	[0x4e] = KEY_KP0,
	[0x4f] = KEY_KPCOMMA,	[0x50] = KEY_KPDOT,	[0x51] = KEY_MUHENKAN,
	[0x60] = KEY_PAUSE,	[0x61] = KEY_SYSRQ,
	[0x62] = KEY_F1,	[0x63] = KEY_F2,	[0x64] = KEY_F3,
	[0x65] = KEY_F4,	[0x66] = KEY_F5,	[0x67] = KEY_F6,
	[0x68] = KEY_F7,	[0x69] = KEY_F8,	[0x6a] = KEY_F9,
	[0x6b] = KEY_F10,	[0x70] = KEY_LEFTSHIFT,	[0x71] = KEY_CAPSLOCK,
	[0x72] = KEY_KATAKANA,	[0x73] = KEY_LEFTALT,	[0x74] = KEY_LEFTCTRL,
	[0x7d] = KEY_RIGHTSHIFT,
};

static struct input_dev *pc98kbd_dev;
static unsigned short pc98kbd_keymap[128];
static DEFINE_SPINLOCK(pc98kbd_lock);
static struct timer_list pc98kbd_timer;

static bool pc98kbd_release_all(void)
{
	bool reported = false;
	int i;

	for (i = 0; i < ARRAY_SIZE(pc98kbd_keymap); i++) {
		unsigned short key = pc98kbd_keymap[i];

		if (key && test_bit(key, pc98kbd_dev->key)) {
			input_report_key(pc98kbd_dev, key, 0);
			reported = true;
		}
	}

	return reported;
}

static bool pc98kbd_report_scancode(u8 raw)
{
	unsigned short key = pc98kbd_keymap[raw & 0x7f];

	if (!key)
		return false;

	if (raw & 0x80) {
		input_report_key(pc98kbd_dev, key, 0);
	} else if (test_bit(key, pc98kbd_dev->key)) {
		/* A repeated make code is the keyboard's hardware repeat. */
		input_event(pc98kbd_dev, EV_KEY, key, 2);
	} else {
		input_report_key(pc98kbd_dev, key, 1);
	}

	return true;
}

/*
 * Drain everything currently available from the one-byte receive register.
 * Return true if this device consumed an interrupt source, even if the byte
 * was an unmapped keyboard command reply.
 */
static bool pc98kbd_drain(void)
{
	bool consumed = false;
	bool reported = false;
	int count;

	for (count = 0; count < PC98KBD_DRAIN_LIMIT; count++) {
		u8 status = inb(PC98KBD_CMD);
		bool error = status & PC98KBD_ST_ERROR;

		if (!(status & (PC98KBD_ST_RXRDY | PC98KBD_ST_ERROR)))
			break;

		if (error) {
			/*
			 * An overrun may have discarded a break code.  Clear
			 * the USART error and release Linux's pressed-key state
			 * so its input state cannot remain stuck.
			 */
			pc98kbd_ctl_write(0x16);
			reported |= pc98kbd_release_all();
			consumed = true;
		}

		if (status & PC98KBD_ST_RXRDY) {
			u8 raw = inb(PC98KBD_DATA);

			consumed = true;
			if (!error)
				reported |= pc98kbd_report_scancode(raw);
		}
	}

	if (reported)
		input_sync(pc98kbd_dev);

	return consumed;
}

static irqreturn_t pc98kbd_interrupt(int irq, void *dev_id)
{
	unsigned long flags;
	bool consumed;

	spin_lock_irqsave(&pc98kbd_lock, flags);
	consumed = pc98kbd_drain();
	spin_unlock_irqrestore(&pc98kbd_lock, flags);

	return consumed ? IRQ_HANDLED : IRQ_NONE;
}

static void pc98kbd_poll(struct timer_list *unused)
{
	unsigned long flags;

	/*
	 * PC-98 keyboard interrupts can be lost while RXRDY remains asserted.
	 * FreeBSD/pc98 uses the same 10 Hz recovery poll so a pending break
	 * code cannot leave a key pressed indefinitely.
	 */
	spin_lock_irqsave(&pc98kbd_lock, flags);
	pc98kbd_drain();
	spin_unlock_irqrestore(&pc98kbd_lock, flags);

	mod_timer(&pc98kbd_timer,
		  jiffies + msecs_to_jiffies(PC98KBD_RECOVERY_MS));
}

static int __init pc98kbd_init(void)
{
	int i, err;

	if (!request_region(PC98KBD_DATA, 1, "pc98kbd-data")) {
		pr_err("pc98kbd: data port 0x%x is busy\n", PC98KBD_DATA);
		return -EBUSY;
	}
	if (!request_region(PC98KBD_CMD, 1, "pc98kbd-command")) {
		pr_err("pc98kbd: command port 0x%x is busy\n", PC98KBD_CMD);
		err = -EBUSY;
		goto err_release_data;
	}

	pc98kbd_dev = input_allocate_device();
	if (!pc98kbd_dev) {
		err = -ENOMEM;
		goto err_release_command;
	}

	pc98kbd_dev->name = "PC-9800 keyboard";
	pc98kbd_dev->phys = "pc98kbd/input0";
	pc98kbd_dev->id.bustype = BUS_HOST;
	pc98kbd_dev->dev.parent = NULL;

	memcpy(pc98kbd_keymap, pc98kbd_keycode, sizeof(pc98kbd_keymap));
	pc98kbd_dev->keycode = pc98kbd_keymap;
	pc98kbd_dev->keycodesize = sizeof(unsigned short);
	pc98kbd_dev->keycodemax = ARRAY_SIZE(pc98kbd_keymap);

	__set_bit(EV_KEY, pc98kbd_dev->evbit);
	for (i = 0; i < ARRAY_SIZE(pc98kbd_keymap); i++)
		if (pc98kbd_keymap[i])
			__set_bit(pc98kbd_keymap[i], pc98kbd_dev->keybit);
	__clear_bit(KEY_RESERVED, pc98kbd_dev->keybit);

	err = input_register_device(pc98kbd_dev);
	if (err) {
		input_free_device(pc98kbd_dev);
		goto err_release_command;
	}

	pc98kbd_hw_init();
	/*
	 * Discard reset replies and recover any scan code received while IRQ1
	 * was still masked.  The input device is registered at this point, so
	 * a genuine key event can also be reported safely.
	 */
	{
		unsigned long flags;

		spin_lock_irqsave(&pc98kbd_lock, flags);
		pc98kbd_drain();
		spin_unlock_irqrestore(&pc98kbd_lock, flags);
	}

	err = request_irq(PC98KBD_IRQ, pc98kbd_interrupt, 0,
			  "pc98kbd", pc98kbd_dev);
	if (err) {
		pr_err("pc98kbd: cannot request IRQ %u: %d\n",
		       PC98KBD_IRQ, err);
		goto err_unregister_input;
	}

	timer_setup(&pc98kbd_timer, pc98kbd_poll, 0);
	mod_timer(&pc98kbd_timer,
		  jiffies + msecs_to_jiffies(PC98KBD_RECOVERY_MS));
	return 0;

err_unregister_input:
	input_unregister_device(pc98kbd_dev);
err_release_command:
	release_region(PC98KBD_CMD, 1);
err_release_data:
	release_region(PC98KBD_DATA, 1);
	return err;
}

static void __exit pc98kbd_exit(void)
{
	timer_delete_sync(&pc98kbd_timer);
	free_irq(PC98KBD_IRQ, pc98kbd_dev);
	input_unregister_device(pc98kbd_dev);
	release_region(PC98KBD_CMD, 1);
	release_region(PC98KBD_DATA, 1);
}

module_init(pc98kbd_init);
module_exit(pc98kbd_exit);

MODULE_DESCRIPTION("NEC PC-9800 keyboard driver");
MODULE_LICENSE("GPL");
