// SPDX-License-Identifier: GPL-2.0
/*
 * NEC PC-9800 built-in uPD8251 serial driver.
 *
 * Based on drivers/serial/8250.c, by Russell King.
 * Based on drivers/char/serial.c, by Linus Torvalds, Theodore Ts'o.
 *
 * Copyright (C) 2002 Osamu Tomita <tomita@cinet.co.jp>
 * Copyright (C) 2026 Awe Morris
 *
 * The hardware state machine, register definitions, and baud-rate divisor
 * calculation are adapted from the last official Linux PC-98 serial98.c.
 * Integration with serial_core, kfifo transmit, and tty_port receive is new
 * for Linux 7.1.  This first stage intentionally supports only the standard
 * non-FIFO onboard interface.
 */

#include <linux/console.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/serial.h>
#include <linux/serial_core.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>

#include <asm/pc9800.h>

#define PC98_SERIAL_NAME	"ttyPC"
#define PC98_SERIAL_PORTS	1

#define PC98_DATA		0x0030
#define PC98_STATUS_COMMAND	0x0032
#define PC98_MODEM_STATUS	0x0033
#define PC98_INTERRUPT_ENABLE2	0x0034
#define PC98_INTERRUPT_ENABLE1	0x0035
#define PC98_INTERRUPT_CONTROL	0x0037
#define PC98_PIT_CHANNEL2	0x0075
#define PC98_PIT_MODE		0x0077
#define PC98_IO_DELAY		0x005f
#define PC98_SERIAL_IRQ		4

#define PC98_COMMAND_RESET	0x40
#define PC98_COMMAND_RTS	0x20
#define PC98_COMMAND_CLEAR_ERROR	0x10
#define PC98_COMMAND_BREAK	0x08
#define PC98_COMMAND_RX_ENABLE	0x04
#define PC98_COMMAND_DTR	0x02
#define PC98_COMMAND_TX_ENABLE	0x01

#define PC98_STATUS_TX_READY	0x01
#define PC98_STATUS_RX_READY	0x02
#define PC98_STATUS_TX_EMPTY	0x04
#define PC98_STATUS_PARITY	0x08
#define PC98_STATUS_OVERRUN	0x10
#define PC98_STATUS_FRAME	0x20
#define PC98_STATUS_BREAK	0x40
#define PC98_STATUS_DSR		0x80

#define PC98_INTERRUPT_TX_READY	0x04
#define PC98_INTERRUPT_TX_EMPTY	0x02
#define PC98_INTERRUPT_RX_READY	0x01

#define PC98_DISABLE_RX_INTERRUPT	0x00
#define PC98_ENABLE_RX_INTERRUPT		0x01
#define PC98_DISABLE_TX_EMPTY_INTERRUPT	0x02
#define PC98_ENABLE_TX_EMPTY_INTERRUPT	0x03
#define PC98_DISABLE_TX_READY_INTERRUPT	0x04
#define PC98_ENABLE_TX_READY_INTERRUPT	0x05

#define PC98_ISR_PASS_LIMIT	256

struct pc9800_8251_port {
	struct uart_port port;
	u8 command;
	u8 mode;
	u8 modem_status;
};

static struct uart_driver pc9800_8251_driver;
static struct platform_device *pc9800_8251_device;

static inline struct pc9800_8251_port *to_pc9800_port(struct uart_port *port)
{
	return container_of(port, struct pc9800_8251_port, port);
}

static void pc9800_8251_command(struct uart_port *port, u8 command)
{
	int i;

	outb(command, PC98_STATUS_COMMAND);
	for (i = 0; i < 4; i++)
		outb(0, PC98_IO_DELAY);
}

static void pc9800_8251_set_mode(struct uart_port *port, u8 mode)
{
	pc9800_8251_command(port, 0);
	pc9800_8251_command(port, 0);
	pc9800_8251_command(port, 0);
	pc9800_8251_command(port, PC98_COMMAND_RESET);
	pc9800_8251_command(port, mode);
}

static void pc9800_8251_stop_tx(struct uart_port *port)
{
	u8 value = inb(PC98_INTERRUPT_ENABLE1);

	value &= ~(PC98_INTERRUPT_TX_READY | PC98_INTERRUPT_TX_EMPTY);
	outb(value, PC98_INTERRUPT_ENABLE1);
}

static bool pc9800_8251_transmit(struct uart_port *port)
{
	struct tty_port *tport = &port->state->port;
	u8 character;

	if (!(inb(PC98_STATUS_COMMAND) & PC98_STATUS_TX_READY))
		return false;

	if (port->x_char) {
		outb(port->x_char, PC98_DATA);
		port->x_char = 0;
		port->icount.tx++;
		return true;
	}

	if (uart_tx_stopped(port) || !uart_fifo_get(port, &character)) {
		pc9800_8251_stop_tx(port);
		return false;
	}

	outb(character, PC98_DATA);
	if (kfifo_len(&tport->xmit_fifo) < WAKEUP_CHARS)
		uart_write_wakeup(port);
	return true;
}

static void pc9800_8251_start_tx(struct uart_port *port)
{
	u8 value = inb(PC98_INTERRUPT_ENABLE1);

	value |= PC98_INTERRUPT_TX_READY | PC98_INTERRUPT_TX_EMPTY;
	outb(value, PC98_INTERRUPT_ENABLE1);
	pc9800_8251_transmit(port);
}

static void pc9800_8251_stop_rx(struct uart_port *port)
{
	port->read_status_mask &= ~PC98_STATUS_RX_READY;
	outb(PC98_DISABLE_RX_INTERRUPT, PC98_INTERRUPT_CONTROL);
}

static bool pc9800_8251_receive(struct uart_port *port)
{
	bool received = false;
	int count;

	for (count = 0; count < PC98_ISR_PASS_LIMIT; count++) {
		u8 status = inb(PC98_STATUS_COMMAND);
		u8 character;
		char flag = TTY_NORMAL;

		if (!(status & PC98_STATUS_RX_READY))
			break;

		character = inb(PC98_DATA);
		port->icount.rx++;
		received = true;

		if (status & PC98_STATUS_BREAK) {
			status &= ~(PC98_STATUS_FRAME | PC98_STATUS_PARITY);
			port->icount.brk++;
			if (uart_handle_break(port))
				continue;
			flag = TTY_BREAK;
		} else if (status & PC98_STATUS_PARITY) {
			port->icount.parity++;
			flag = TTY_PARITY;
		} else if (status & PC98_STATUS_FRAME) {
			port->icount.frame++;
			flag = TTY_FRAME;
		}
		if (status & PC98_STATUS_OVERRUN)
			port->icount.overrun++;

		if (status & (PC98_STATUS_PARITY | PC98_STATUS_OVERRUN |
			      PC98_STATUS_FRAME | PC98_STATUS_BREAK)) {
			u8 command = to_pc9800_port(port)->command |
				     PC98_COMMAND_CLEAR_ERROR;

			pc9800_8251_command(port, command);
		}

		if (!uart_handle_sysrq_char(port, character))
			uart_insert_char(port, status, PC98_STATUS_OVERRUN,
					 character, flag);
	}

	return received;
}

static irqreturn_t pc9800_8251_interrupt(int irq, void *data)
{
	struct uart_port *port = data;
	unsigned long flags;
	bool handled = false;
	bool received = false;
	int pass;

	uart_port_lock_irqsave(port, &flags);
	for (pass = 0; pass < PC98_ISR_PASS_LIMIT; pass++) {
		u8 status = inb(PC98_STATUS_COMMAND);
		bool progress = false;

		if (status & PC98_STATUS_RX_READY) {
			bool rx = pc9800_8251_receive(port);

			progress |= rx;
			received |= rx;
		}
		if (status & PC98_STATUS_TX_READY)
			progress |= pc9800_8251_transmit(port);
		if (!progress)
			break;
		handled = true;
	}
	uart_port_unlock_irqrestore(port, flags);
	if (received)
		tty_flip_buffer_push(&port->state->port);

	return handled ? IRQ_HANDLED : IRQ_NONE;
}

static unsigned int pc9800_8251_tx_empty(struct uart_port *port)
{
	return (inb(PC98_STATUS_COMMAND) & PC98_STATUS_TX_EMPTY) ?
		TIOCSER_TEMT : 0;
}

static u8 pc9800_8251_read_modem_status(struct pc9800_8251_port *up)
{
	u8 input = inb(PC98_MODEM_STATUS);
	u8 status = inb(PC98_STATUS_COMMAND);
	u8 modem = 0;

	if (!(input & 0x20))
		modem |= UART_MSR_DCD;
	if (!(input & 0x80))
		modem |= UART_MSR_RI;
	if (!(input & 0x40))
		modem |= UART_MSR_CTS;
	if (status & PC98_STATUS_DSR)
		modem |= UART_MSR_DSR;

	up->modem_status = ((up->modem_status ^ modem) >> 4) | modem;
	return up->modem_status;
}

static unsigned int pc9800_8251_get_mctrl(struct uart_port *port)
{
	u8 status = pc9800_8251_read_modem_status(to_pc9800_port(port));
	unsigned int result = 0;

	if (status & UART_MSR_DCD)
		result |= TIOCM_CAR;
	if (status & UART_MSR_RI)
		result |= TIOCM_RNG;
	if (status & UART_MSR_DSR)
		result |= TIOCM_DSR;
	if (status & UART_MSR_CTS)
		result |= TIOCM_CTS;
	return result;
}

static void pc9800_8251_set_mctrl(struct uart_port *port,
				  unsigned int control)
{
	struct pc9800_8251_port *up = to_pc9800_port(port);

	up->command &= ~(PC98_COMMAND_RTS | PC98_COMMAND_DTR);
	if (control & TIOCM_RTS)
		up->command |= PC98_COMMAND_RTS;
	if (control & TIOCM_DTR)
		up->command |= PC98_COMMAND_DTR;
	pc9800_8251_command(port, up->command);
}

static void pc9800_8251_enable_ms(struct uart_port *port)
{
	outb(inb(PC98_INTERRUPT_ENABLE2) | 0x80,
	     PC98_INTERRUPT_ENABLE2);
}

static void pc9800_8251_break_ctl(struct uart_port *port, int state)
{
	struct pc9800_8251_port *up = to_pc9800_port(port);
	unsigned long flags;

	uart_port_lock_irqsave(port, &flags);
	if (state == -1)
		up->command |= PC98_COMMAND_BREAK;
	else
		up->command &= ~PC98_COMMAND_BREAK;
	pc9800_8251_command(port, up->command);
	uart_port_unlock_irqrestore(port, flags);
}

static int pc9800_8251_startup(struct uart_port *port)
{
	struct pc9800_8251_port *up = to_pc9800_port(port);
	int error;

	up->mode = 0xfc;
	pc9800_8251_set_mode(port, up->mode);
	outb(PC98_DISABLE_RX_INTERRUPT, PC98_INTERRUPT_CONTROL);
	outb(PC98_DISABLE_TX_EMPTY_INTERRUPT, PC98_INTERRUPT_CONTROL);
	outb(PC98_DISABLE_TX_READY_INTERRUPT, PC98_INTERRUPT_CONTROL);
	up->mode = 0;
	pc9800_8251_set_mode(port, up->mode);

	inb(PC98_DATA);
	inb(PC98_STATUS_COMMAND);

	error = request_irq(port->irq, pc9800_8251_interrupt, 0,
			    "pc9800_8251", port);
	if (error)
		return error;

	up->mode = 0x4e;
	pc9800_8251_set_mode(port, up->mode);
	up->command = PC98_COMMAND_RTS | PC98_COMMAND_RX_ENABLE |
		      PC98_COMMAND_DTR | PC98_COMMAND_TX_ENABLE;
	pc9800_8251_command(port, up->command);

	outb(0, PC98_INTERRUPT_ENABLE2);
	outb(PC98_ENABLE_RX_INTERRUPT, PC98_INTERRUPT_CONTROL);
	return 0;
}

static void pc9800_8251_shutdown(struct uart_port *port)
{
	struct pc9800_8251_port *up = to_pc9800_port(port);
	unsigned long flags;

	uart_port_lock_irqsave(port, &flags);
	outb(0x80, PC98_INTERRUPT_ENABLE2);
	outb(0, PC98_INTERRUPT_ENABLE2);
	outb(PC98_DISABLE_RX_INTERRUPT, PC98_INTERRUPT_CONTROL);
	outb(PC98_DISABLE_TX_EMPTY_INTERRUPT, PC98_INTERRUPT_CONTROL);
	outb(PC98_DISABLE_TX_READY_INTERRUPT, PC98_INTERRUPT_CONTROL);
	up->command = 0;
	pc9800_8251_set_mode(port, up->mode);
	uart_port_unlock_irqrestore(port, flags);

	free_irq(port->irq, port);
	inb(PC98_DATA);
}

static void pc9800_8251_set_termios(struct uart_port *port,
				    struct ktermios *termios,
				    const struct ktermios *old)
{
	struct pc9800_8251_port *up = to_pc9800_port(port);
	unsigned int baud, divisor;
	unsigned long flags;
	bool five_bits = false;
	u8 mode;

	switch (termios->c_cflag & CSIZE) {
	case CS5:
		mode = 0x42;
		five_bits = true;
		break;
	case CS6:
		mode = 0x46;
		break;
	case CS7:
		mode = 0x4a;
		break;
	default:
		mode = 0x4e;
		termios->c_cflag &= ~CSIZE;
		termios->c_cflag |= CS8;
		break;
	}
	if (termios->c_cflag & CSTOPB)
		mode ^= five_bits ? 0xc0 : 0x80;
	if (termios->c_cflag & PARENB)
		mode |= 0x10;
	if (!(termios->c_cflag & PARODD))
		mode |= 0x20;

	baud = uart_get_baud_rate(port, termios, old, 0,
				  port->uartclk / 16);
	divisor = max(uart_get_divisor(port, baud) / 3, 1U);

	uart_port_lock_irqsave(port, &flags);
	uart_update_timeout(port, termios->c_cflag, baud);
	port->read_status_mask = PC98_STATUS_RX_READY |
		PC98_STATUS_OVERRUN | PC98_STATUS_TX_EMPTY;
	if (termios->c_iflag & INPCK)
		port->read_status_mask |= PC98_STATUS_FRAME |
			PC98_STATUS_PARITY;
	if (termios->c_iflag & (BRKINT | PARMRK))
		port->read_status_mask |= PC98_STATUS_BREAK;

	port->ignore_status_mask = 0;
	if (termios->c_iflag & IGNPAR)
		port->ignore_status_mask |= PC98_STATUS_FRAME |
			PC98_STATUS_PARITY;
	if (termios->c_iflag & IGNBRK) {
		port->ignore_status_mask |= PC98_STATUS_BREAK;
		if (termios->c_iflag & IGNPAR)
			port->ignore_status_mask |= PC98_STATUS_OVERRUN;
	}
	if (!(termios->c_cflag & CREAD))
		port->ignore_status_mask |= PC98_STATUS_RX_READY;

	up->mode = mode;
	pc9800_8251_set_mode(port, up->mode);
	outb(0xb6, PC98_PIT_MODE);
	outb(divisor & 0xff, PC98_PIT_CHANNEL2);
	outb((divisor >> 8) & 0xff, PC98_PIT_CHANNEL2);

	up->command = PC98_COMMAND_RTS | PC98_COMMAND_RX_ENABLE |
		      PC98_COMMAND_DTR | PC98_COMMAND_TX_ENABLE;
	pc9800_8251_command(port, up->command);
	outb(0, PC98_INTERRUPT_ENABLE2);
	outb(PC98_ENABLE_RX_INTERRUPT, PC98_INTERRUPT_CONTROL);
	uart_port_unlock_irqrestore(port, flags);
}

static const char *pc9800_8251_type(struct uart_port *port)
{
	return port->type == PORT_PC9800_8251 ?
		"NEC PC-9800 onboard uPD8251" : NULL;
}

static int pc9800_8251_request_port(struct uart_port *port)
{
	if (!request_region(PC98_DATA, 1, "pc9800_8251 data"))
		return -EBUSY;
	if (!request_region(PC98_STATUS_COMMAND, 1, "pc9800_8251 command")) {
		release_region(PC98_DATA, 1);
		return -EBUSY;
	}
	return 0;
}

static void pc9800_8251_release_port(struct uart_port *port)
{
	release_region(PC98_STATUS_COMMAND, 1);
	release_region(PC98_DATA, 1);
}

static void pc9800_8251_config_port(struct uart_port *port, int flags)
{
	if (flags & UART_CONFIG_TYPE)
		port->type = PORT_PC9800_8251;
}

static int pc9800_8251_verify_port(struct uart_port *port,
				   struct serial_struct *serial)
{
	return -EINVAL;
}

static const struct uart_ops pc9800_8251_ops = {
	.tx_empty = pc9800_8251_tx_empty,
	.set_mctrl = pc9800_8251_set_mctrl,
	.get_mctrl = pc9800_8251_get_mctrl,
	.stop_tx = pc9800_8251_stop_tx,
	.start_tx = pc9800_8251_start_tx,
	.stop_rx = pc9800_8251_stop_rx,
	.enable_ms = pc9800_8251_enable_ms,
	.break_ctl = pc9800_8251_break_ctl,
	.startup = pc9800_8251_startup,
	.shutdown = pc9800_8251_shutdown,
	.set_termios = pc9800_8251_set_termios,
	.type = pc9800_8251_type,
	.release_port = pc9800_8251_release_port,
	.request_port = pc9800_8251_request_port,
	.config_port = pc9800_8251_config_port,
	.verify_port = pc9800_8251_verify_port,
};

static struct pc9800_8251_port pc9800_8251_ports[PC98_SERIAL_PORTS] = {
	{
		.port = {
			.iobase = PC98_DATA,
			.mapbase = PC98_DATA,
			.iotype = UPIO_PORT,
			.irq = PC98_SERIAL_IRQ,
			.fifosize = 1,
			.ops = &pc9800_8251_ops,
			.flags = UPF_BOOT_AUTOCONF,
			.line = 0,
			.type = PORT_PC9800_8251,
		},
	},
};

#ifdef CONFIG_SERIAL_PC98_8251_CONSOLE
static bool pc9800_8251_console_wait(struct uart_port *port)
{
	unsigned int timeout = 10000;

	while ((inb(PC98_STATUS_COMMAND) &
		(PC98_STATUS_TX_READY | PC98_STATUS_TX_EMPTY)) !=
	       (PC98_STATUS_TX_READY | PC98_STATUS_TX_EMPTY)) {
		if (!--timeout)
			return false;
		udelay(1);
	}
	return true;
}

static void pc9800_8251_console_putchar(struct uart_port *port,
					unsigned char character)
{
	if (pc9800_8251_console_wait(port))
		outb(character, PC98_DATA);
}

static void pc9800_8251_console_write(struct console *console,
				      const char *string,
				      unsigned int count)
{
	struct uart_port *port = &pc9800_8251_ports[console->index].port;
	u8 interrupt_enable1 = inb(PC98_INTERRUPT_ENABLE1);
	u8 interrupt_enable2 = inb(PC98_INTERRUPT_ENABLE2);

	outb(0x80, PC98_INTERRUPT_ENABLE2);
	outb(0, PC98_INTERRUPT_ENABLE2);
	outb(PC98_DISABLE_RX_INTERRUPT, PC98_INTERRUPT_CONTROL);
	outb(PC98_DISABLE_TX_EMPTY_INTERRUPT, PC98_INTERRUPT_CONTROL);
	outb(PC98_DISABLE_TX_READY_INTERRUPT, PC98_INTERRUPT_CONTROL);

	uart_console_write(port, string, count,
			   pc9800_8251_console_putchar);
	pc9800_8251_console_wait(port);

	outb(0, PC98_INTERRUPT_ENABLE2);
	if (interrupt_enable1 & PC98_INTERRUPT_RX_READY)
		outb(PC98_ENABLE_RX_INTERRUPT, PC98_INTERRUPT_CONTROL);
	if (interrupt_enable1 & PC98_INTERRUPT_TX_EMPTY)
		outb(PC98_ENABLE_TX_EMPTY_INTERRUPT, PC98_INTERRUPT_CONTROL);
	if (interrupt_enable1 & PC98_INTERRUPT_TX_READY)
		outb(PC98_ENABLE_TX_READY_INTERRUPT, PC98_INTERRUPT_CONTROL);
	outb(interrupt_enable2, PC98_INTERRUPT_ENABLE2);
}

static int __init pc9800_8251_console_setup(struct console *console,
					    char *options)
{
	struct uart_port *port = &pc9800_8251_ports[0].port;
	int baud = 9600;
	int parity = 'n';
	int bits = 8;
	int flow = 'n';

	if (console->index < 0 || console->index >= PC98_SERIAL_PORTS)
		console->index = 0;
	if (options)
		uart_parse_options(options, &baud, &parity, &bits, &flow);
	return uart_set_options(port, console, baud, parity, bits, flow);
}

static struct console pc9800_8251_console = {
	.name = PC98_SERIAL_NAME,
	.write = pc9800_8251_console_write,
	.device = uart_console_device,
	.setup = pc9800_8251_console_setup,
	.flags = CON_PRINTBUFFER,
	.index = -1,
	.data = &pc9800_8251_driver,
};

static int __init pc9800_8251_console_init(void)
{
	struct uart_port *port = &pc9800_8251_ports[0].port;

	port->uartclk = (pc9800_pit_tick_rate == 1996800) ?
		374400 * 16 : 460800 * 16;
	spin_lock_init(&port->lock);
	register_console(&pc9800_8251_console);
	return 0;
}
console_initcall(pc9800_8251_console_init);

#define PC9800_8251_CONSOLE	(&pc9800_8251_console)
#else
#define PC9800_8251_CONSOLE	NULL
#endif

static struct uart_driver pc9800_8251_driver = {
	.owner = THIS_MODULE,
	.driver_name = "pc9800_8251",
	.dev_name = PC98_SERIAL_NAME,
	.major = 0,
	.minor = 0,
	.nr = PC98_SERIAL_PORTS,
	.cons = PC9800_8251_CONSOLE,
};

static int __init pc9800_8251_init(void)
{
	struct uart_port *port = &pc9800_8251_ports[0].port;
	int error;

	pc9800_8251_device =
		platform_device_register_simple("pc9800-8251", -1, NULL, 0);
	if (IS_ERR(pc9800_8251_device))
		return PTR_ERR(pc9800_8251_device);

	port->dev = &pc9800_8251_device->dev;
	port->uartclk = (pc9800_pit_tick_rate == 1996800) ?
		374400 * 16 : 460800 * 16;
	error = uart_register_driver(&pc9800_8251_driver);
	if (error)
		goto unregister_device;

	error = uart_add_one_port(&pc9800_8251_driver, port);
	if (error) {
		uart_unregister_driver(&pc9800_8251_driver);
		goto unregister_device;
	}
	return 0;

unregister_device:
	platform_device_unregister(pc9800_8251_device);
	pc9800_8251_device = NULL;
	return error;
}

static void __exit pc9800_8251_exit(void)
{
	uart_remove_one_port(&pc9800_8251_driver,
			     &pc9800_8251_ports[0].port);
	uart_unregister_driver(&pc9800_8251_driver);
	platform_device_unregister(pc9800_8251_device);
}

module_init(pc9800_8251_init);
module_exit(pc9800_8251_exit);

MODULE_AUTHOR("Osamu Tomita <tomita@cinet.co.jp>");
MODULE_AUTHOR("Awe Morris");
MODULE_DESCRIPTION("NEC PC-9800 onboard uPD8251 serial driver");
MODULE_LICENSE("GPL");
