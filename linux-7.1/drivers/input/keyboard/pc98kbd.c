// SPDX-License-Identifier: GPL-2.0
/*
 * Keyboard driver for the NEC PC-9800.
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
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/module.h>

#define PC98KBD_DATA	0x41	/* R: scancode  W: mode / ctrl-send */
#define PC98KBD_CMD	0x43	/* R: status    W: command */
#define PC98KBD_ST_RXRDY 0x02	/* status bit: a scancode is ready */
#define PC98KBD_IRQ	1

static void pc98kbd_hw_init(void)
{
	/*
	 * Reset the uPD8251 from an unknown firmware state, select the PC-98
	 * keyboard's asynchronous mode, then release RTS and enable reception.
	 * The intermediate command matches NEC firmware initialization.
	 */
	outb(0x00, PC98KBD_CMD);
	outb(0x00, PC98KBD_CMD);
	outb(0x00, PC98KBD_CMD);
	outb(0x40, PC98KBD_CMD);
	outb(0x3a, PC98KBD_CMD);
	outb(0x32, PC98KBD_CMD);
	outb(0x16, PC98KBD_CMD);
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

static irqreturn_t pc98kbd_interrupt(int irq, void *dev_id)
{
	int reported = 0;

	while (inb(PC98KBD_CMD) & PC98KBD_ST_RXRDY) {
		u8 raw = inb(PC98KBD_DATA);
		unsigned short key = pc98kbd_keymap[raw & 0x7f];

		if (key) {
			input_report_key(pc98kbd_dev, key, !(raw & 0x80));
			reported = 1;
		}
	}
	if (reported)
		input_sync(pc98kbd_dev);

	return reported ? IRQ_HANDLED : IRQ_NONE;
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
	__set_bit(EV_REP, pc98kbd_dev->evbit);
	for (i = 0; i < ARRAY_SIZE(pc98kbd_keymap); i++)
		if (pc98kbd_keymap[i])
			__set_bit(pc98kbd_keymap[i], pc98kbd_dev->keybit);
	__clear_bit(KEY_RESERVED, pc98kbd_dev->keybit);

	err = input_register_device(pc98kbd_dev);
	if (err) {
		input_free_device(pc98kbd_dev);
		goto err_release_command;
	}

	err = request_irq(PC98KBD_IRQ, pc98kbd_interrupt, 0,
			  "pc98kbd", pc98kbd_dev);
	if (err) {
		pr_err("pc98kbd: cannot request IRQ %u: %d\n",
		       PC98KBD_IRQ, err);
		goto err_unregister_input;
	}

	pc98kbd_hw_init();
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
	free_irq(PC98KBD_IRQ, pc98kbd_dev);
	input_unregister_device(pc98kbd_dev);
	release_region(PC98KBD_CMD, 1);
	release_region(PC98KBD_DATA, 1);
}

module_init(pc98kbd_init);
module_exit(pc98kbd_exit);

MODULE_DESCRIPTION("NEC PC-9800 keyboard driver");
MODULE_LICENSE("GPL");
