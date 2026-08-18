// SPDX-License-Identifier: GPL-2.0
/*
 * NEC PC-9800 platform support.
 *
 * Reset: writing anything to I/O 0xF0 resets the CPU. None of the generic x86
 * methods apply — 0x64 is not a keyboard controller here, there is no CF9
 * register and no ACPI — so the restart hooks are replaced outright rather
 * than added to the list native_machine_emergency_restart() walks.
 *
 * Clock: the RTC is a uPD4990A, not a CMOS one. It is driven serially through
 * I/O 0x20 (command bits, plus CLK and STB) and read back a bit at a time from
 * bit 0 of I/O 0x33. Replacing get_wallclock also stops the generic code from
 * touching 0x70/0x71, which on this machine are the PIT's ports.
 *
 * Platform resource allocation and machine hooks are derived from the last
 * official Linux PC-9800 port by Osamu Tomita <tomita@cinet.co.jp>.
 * Copyright (C) 2026 Awe Morris
 */
#include <linux/bcd.h>
#include <linux/export.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/time.h>
#include <linux/timex.h>

#include <asm/pc9800.h>
#include <asm/reboot.h>
#include <asm/setup_data.h>
#include <asm/x86_init.h>

#define PC98_RESET_PORT	0xf0

/* BIOS work area. Bit 7 of the flag byte marks an 8MHz-family machine. */
#define PC98_BIOS_FLAG	0x501
#define BIOS_FLAG_8MHZ	0x80

#define PC98_CLOCK_5MHZ	2457600ul
#define PC98_CLOCK_8MHZ	1996800ul

/* Both the PIT and the 8251's baud generator run off this. */
unsigned long pc9800_pit_tick_rate = PC98_CLOCK_5MHZ;
EXPORT_SYMBOL_GPL(pc9800_pit_tick_rate);

#define PC9800_MAX_DISK_GEOMETRIES	12

struct pc9800_disk_geometry {
	u8 bios_drive;
	u8 heads;
	u8 sectors;
	u8 flags;
};

static struct pc9800_disk_geometry
	pc9800_disk_geometries[PC9800_MAX_DISK_GEOMETRIES];
static unsigned int pc9800_disk_geometry_count;
static int pc9800_boot_geometry_index = -1;

void __init pc9800_set_boot_disk_info(const struct pc98_boot_disk_setup *info)
{
	struct pc9800_disk_geometry *geometry = NULL;
	unsigned int i;

	if (info->magic != PC98_BOOT_DISK_MAGIC ||
	    info->version != PC98_BOOT_DISK_VERSION ||
	    info->size < sizeof(*info) || !info->heads || !info->sectors)
		return;

	for (i = 0; i < pc9800_disk_geometry_count; i++) {
		if (pc9800_disk_geometries[i].bios_drive == info->bios_drive) {
			geometry = &pc9800_disk_geometries[i];
			break;
		}
	}
	if (!geometry) {
		if (pc9800_disk_geometry_count >= PC9800_MAX_DISK_GEOMETRIES) {
			pr_warn("PC-98 disk geometry table is full; ignoring BIOS drive %02x\n",
				info->bios_drive);
			return;
		}
		i = pc9800_disk_geometry_count++;
		geometry = &pc9800_disk_geometries[i];
	}

	geometry->bios_drive = info->bios_drive;
	geometry->heads = info->heads;
	geometry->sectors = info->sectors;
	geometry->flags = info->flags;

	/* A single record from an older loader always describes the boot disk. */
	if (pc9800_boot_geometry_index < 0 ||
	    info->flags & PC98_BOOT_DISK_F_BOOT)
		pc9800_boot_geometry_index = i;

	pr_info("PC-98 disk: BIOS drive %02x, logical CHS */%u/%u%s%s\n",
		geometry->bios_drive, geometry->heads, geometry->sectors,
		geometry->flags & PC98_BOOT_DISK_F_BOOT ? " (boot)" : "",
		geometry->flags & PC98_BOOT_DISK_F_FALLBACK ?
		" (fallback)" : "");
}

bool pc9800_get_boot_disk_geometry(unsigned int *heads,
				   unsigned int *sectors)
{
	const struct pc9800_disk_geometry *geometry;

	if (pc9800_boot_geometry_index < 0)
		return false;

	geometry = &pc9800_disk_geometries[pc9800_boot_geometry_index];
	*heads = geometry->heads;
	*sectors = geometry->sectors;
	return true;
}
EXPORT_SYMBOL_GPL(pc9800_get_boot_disk_geometry);

bool pc9800_get_boot_disk_geometry_for(unsigned int bios_base,
				       unsigned int unit,
				       unsigned int *heads,
				       unsigned int *sectors)
{
	u8 bios_drive = (bios_base & 0xf8) | (unit & 7);
	unsigned int i;

	for (i = 0; i < pc9800_disk_geometry_count; i++) {
		if (pc9800_disk_geometries[i].bios_drive != bios_drive)
			continue;
		*heads = pc9800_disk_geometries[i].heads;
		*sectors = pc9800_disk_geometries[i].sectors;
		return true;
	}
	return false;
}
EXPORT_SYMBOL_GPL(pc9800_get_boot_disk_geometry_for);

#define PC98_RTC_CTRL	0x20		/* W: command bits, CLK, STB, data */
#define PC98_RTC_MODE	0x22		/* bit 5: uPD4993 extended format */
#define PC98_RTC_DATA	0x33		/* R: bit 0 is the serial output */
#define PC98_IO_WAIT	0x5f		/* the machine's own I/O delay port */

#define RTC_STB		0x08
#define RTC_CLK		0x10

#define RTC_CMD_HOLD	0x00
#define RTC_CMD_SHIFT	0x01
#define RTC_CMD_READ	0x03

static void pc9800_emergency_restart(void)
{
	for (;;) {
		outb(0, PC98_RESET_PORT);
		native_halt();
	}
}

static void pc9800_restart(char *cmd)
{
	pc9800_emergency_restart();
}

/*
 * The RTC wants a couple of microseconds per edge, and udelay() is not
 * calibrated yet when the wallclock is first read, so spend the time the way
 * the machine itself does.
 */
static void pc9800_io_wait(void)
{
	outb(0, PC98_IO_WAIT);
	outb(0, PC98_IO_WAIT);
}

static void pc9800_rtc_cmd(u8 cmd)
{
	outb(cmd, PC98_RTC_CTRL);		/* select the command */
	pc9800_io_wait();
	outb(cmd | RTC_STB, PC98_RTC_CTRL);	/* strobe runs it */
	pc9800_io_wait();
	outb(cmd, PC98_RTC_CTRL);
	pc9800_io_wait();
}

/* Registers shift out least significant bit first, seconds first. */
static u8 pc9800_rtc_shift_byte(void)
{
	u8 val = 0;
	int i;

	for (i = 0; i < 8; i++) {
		if (inb(PC98_RTC_DATA) & 1)
			val |= 1 << i;
		outb(RTC_CLK, PC98_RTC_CTRL);
		pc9800_io_wait();
		outb(0, PC98_RTC_CTRL);
		pc9800_io_wait();
	}
	return val;
}

static void pc9800_get_wallclock(struct timespec64 *now)
{
	u8 sec, min, hour, day, month_week, year;
	unsigned int y;

	/*
	 * Later PC-98 models can expose the uPD4993 extended serial format,
	 * which prefixes the usual 48-bit uPD4990A payload with four bits.
	 * Firmware is free to leave that mode selected.  Select the format
	 * decoded below explicitly instead of inheriting the firmware state.
	 */
	outb(0, PC98_RTC_MODE);
	pc9800_io_wait();

	pc9800_rtc_cmd(RTC_CMD_READ);		/* latch the current time */
	pc9800_rtc_cmd(RTC_CMD_SHIFT);		/* and start clocking it out */

	sec		= pc9800_rtc_shift_byte();
	min		= pc9800_rtc_shift_byte();
	hour		= pc9800_rtc_shift_byte();
	day		= pc9800_rtc_shift_byte();
	month_week	= pc9800_rtc_shift_byte();
	year		= pc9800_rtc_shift_byte();

	pc9800_rtc_cmd(RTC_CMD_HOLD);

	/* Everything is BCD except the month, which is a plain 1-12 in the top
	 * nibble with the weekday below it. The year has two digits. */
	y = bcd2bin(year);
	y += (y < 80) ? 2000 : 1900;

	now->tv_sec = mktime64(y, month_week >> 4, bcd2bin(day),
			       bcd2bin(hour), bcd2bin(min), bcd2bin(sec));
	now->tv_nsec = 0;
}

static int pc9800_set_wallclock(const struct timespec64 *now)
{
	/* Writing the uPD4990A back is not implemented: the emulator ignores
	 * the time-set command, so there is no way to test it here. */
	return -ENODEV;
}

void __init pc9800_init_platform(void)
{
	u8 flag = readb((void __iomem *)(__ISA_IO_base + PC98_BIOS_FLAG));

	/* Runs from setup_arch(), so this lands before the PIT is programmed
	 * and before the TSC is calibrated against it. */
	if (flag & BIOS_FLAG_8MHZ)
		pc9800_pit_tick_rate = PC98_CLOCK_8MHZ;

	/* No CMOS RTC and no 8042 on this machine. Letting the generic code
	 * poke at them would be actively harmful: 0x71 is a PIT counter and
	 * 0x60 is the text GDC. */
	x86_platform.legacy.rtc = 0;
	x86_platform.legacy.i8042 = X86_LEGACY_I8042_PLATFORM_ABSENT;

	x86_platform.get_wallclock = pc9800_get_wallclock;
	x86_platform.set_wallclock = pc9800_set_wallclock;
}

static int __init pc9800_reboot_init(void)
{
	machine_ops.restart = pc9800_restart;
	machine_ops.emergency_restart = pc9800_emergency_restart;
	return 0;
}
arch_initcall(pc9800_reboot_init);
