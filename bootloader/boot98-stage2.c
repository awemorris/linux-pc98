/*
 * PC-9800 Bootloader
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "boot98-abi.h"
#include "boot98-console.h"
#include "boot98-env.h"
#include "boot98-beui-pc98-auto.h"
#include "boot98-fat16.h"
#include "boot98-fs.h"
#include "boot98-image.h"
#include "boot98-messages.h"
#include "boot98-namespace.h"
#include "boot98-noct-napi.h"
#include "boot98-noct-platform.h"

#define MAX_PARTS 16
#define CFG_MAX 8192
#define LINE_MAX 256
#define BP_ADDR 0x80000U
#define CMD_ADDR 0x81000U
#define PC98_ADDR 0x82000U
#define PC98_SETUP_NODE_SIZE 32U
#define STARTUP_TIMEOUT_SECONDS 1
#define MAX_IDE_DEVICES 4
#define MAX_SCSI_TARGETS 7
#define MAX_FIXED_DEVICES (MAX_IDE_DEVICES + MAX_SCSI_TARGETS)
#define PC98_WA_IDE_DRIVES 0x055dU
#define PC98_WA_SCSI_TARGETS 0x0482U
#define PC98_WA_SCSI_ROM0 0x04b2U
#define PC98_WA_SCSI_ROM1 0x04baU
#define PC98_WA_SCSI_ROM2 0x04bcU

/*
 * Stage 2 runs without a C library or operating-system services.  The request
 * object is the sole mutable argument passed through the real-mode BIOS
 * gateway in Stage 1.
 */
static boot98_bios_gateway_t gw;
static const struct boot98_handoff *ho;
static const struct boot98_device *devs;
static struct boot98_device discovered_devices[MAX_FIXED_DEVICES];
static unsigned device_count;
static struct boot98_bios_request rq;
static uint8_t sec[512], cfg[CFG_MAX];
#ifdef BOOT98_M9_WRITE_TEST
static uint8_t m9_original[512], m9_pattern[512], m9_observed[512];
#endif
static uint32_t load_text_done, load_text_total;
static uint32_t load_data_done, load_data_total;
static int load_progress_class = -1;

struct part {
	uint8_t valid, index, bootable;
	char name[17];
	uint32_t start, data;
};

enum startup_phase {
	STARTUP_DRAW,
	STARTUP_PROBE,
	STARTUP_TIMEOUT,
	STARTUP_SELECTED,
	STARTUP_SHELL,
};

enum startup_auto_kind {
	STARTUP_AUTO_NONE,
	STARTUP_AUTO_CONFIG,
	STARTUP_AUTO_PBR,
};

enum startup_config_kind {
	STARTUP_CONFIG_NONE,
	STARTUP_CONFIG_AUTOEXEC,
	STARTUP_CONFIG_BOOTCFG,
};

struct startup_state {
	enum startup_phase phase;
	unsigned next_candidate;
	unsigned probe_total;
	unsigned probe_done;
	unsigned fixed_count;
	uint8_t ide_bitmap;
	uint8_t scsi_bitmap;
	int auto_device;
	int auto_partition;
	int auto_priority;
	enum startup_auto_kind auto_kind;
	enum startup_config_kind auto_config_kind;
	int automatic_cancelled;
	int timeout_start;
	unsigned timeout_budget;
};
static struct boot98_filesystem mounted_fs;
static struct boot98_namespace mounted_namespace;
static struct boot98_environment boot_environment;
static struct boot98_beui_pc98_auto beui_display;
static struct boot98_beui_hal beui_hal;
static struct part parts[MAX_PARTS];
static int curdev = -1, curpart = -1;
static char kernel_name[BOOT98_PATH_MAX], kernel_arg[256];
static void findboot(void);

/* Minimal freestanding string and memory primitives. */
static void memzero(void *p, uint32_t n)
{
	uint8_t *q = p;
	while (n--)
		*q++ = 0;
}
static void memcopy(void *d, const void *s, uint32_t n)
{
	uint8_t *a = d;
	const uint8_t *b = s;
	while (n--)
		*a++ = *b++;
}
static int streq(const char *a, const char *b)
{
	while (*a && *a == *b)
		a++, b++;
	return *a == *b;
}
static unsigned slen(const char *s)
{
	unsigned n = 0;
	while (s[n])
		n++;
	return n;
}
static int strcopy(char *destination, const char *source, unsigned capacity)
{
	unsigned i = 0;

	if (!capacity)
		return 0;
	while (source[i] && i + 1 < capacity) {
		destination[i] = source[i];
		i++;
	}
	destination[i] = 0;
	if (source[i]) {
		destination[0] = 0;
		return 0;
	}
	return 1;
}

static void update_cursor(void)
{
	boot98_console_update_cursor();
}

static void putc(char c)
{
	boot98_console_putc((uint8_t)c);
}
static void puts(const char *s)
{
	boot98_console_puts_sjis((const uint8_t *)s);
}
static void hex8(uint8_t v)
{
	const char *h = "0123456789ABCDEF";
	putc(h[v >> 4]);
	putc(h[v & 15]);
}
static void dec(unsigned v)
{
	char b[11];
	unsigned n = 0;
	if (!v) {
		putc('0');
		return;
	}
	while (v) {
		b[n++] = '0' + v % 10;
		v /= 10;
	}
	while (n)
		putc(b[--n]);
}

static unsigned kib(uint32_t bytes)
{
	return (bytes >> 10) + !!(bytes & 1023);
}

/* Rewrite the current terminal line without scrolling for each disk read. */
static void show_load_progress(int load_class)
{
	uint32_t done = load_class ? load_data_done : load_text_done;
	uint32_t total = load_class ? load_data_total : load_text_total;

	if (load_progress_class >= 0 && load_progress_class != load_class)
		putc('\n');
	load_progress_class = load_class;
	putc('\r');
	boot98_console_clear_to_eol();
	putc('\r');
	puts((const char *)(load_class ? boot98_msg_data : boot98_msg_code));
	dec(kib(done));
	puts(" / ");
	dec(kib(total));
	puts(" KB");
	if (done >= total) {
		putc('\n');
		load_progress_class = -1;
	}
}

static void begin_load_progress(uint32_t kernel_size)
{
	puts((const char *)boot98_msg_kernel_size);
	dec(kib(kernel_size));
	puts(" KB\n");
	load_progress_class = -1;
}

/* Stage 1 BIOS gateway and little-endian disk-field helpers. */
static uint32_t call(uint16_t svc)
{
	rq.service = svc;
	return gw(&rq);
}

static int
beui_display_reset(void *context)
{
	(void)context;
	return call(BOOT98_BIOS_DISPLAY_RESET) == 0;
}

static int
beui_display_stop(void *context)
{
	(void)context;
	return call(BOOT98_BIOS_DISPLAY_STOP) == 0;
}
static int readsec(const struct boot98_device *d, uint32_t lba, void *buf)
{
	rq.bios_id = d->bios_id;
	rq.heads = d->heads;
	rq.sectors = d->sectors;
	rq.lba = lba;
	rq.buffer = (uint32_t)buf;
	return call(BOOT98_BIOS_DISK_READ) != 0;
}
static int writesec(const struct boot98_device *d, uint32_t lba,
		    const void *buf)
{
	rq.bios_id = d->bios_id;
	rq.heads = d->heads;
	rq.sectors = d->sectors;
	rq.lba = lba;
	rq.buffer = (uint32_t)buf;
	return call(BOOT98_BIOS_DISK_WRITE) != 0;
}
static uint16_t w16(const uint8_t *p)
{
	return p[0] | ((uint16_t)p[1] << 8);
}
static uint32_t w32(const uint8_t *p)
{
	return w16(p) | ((uint32_t)w16(p + 2) << 16);
}
static uint32_t chs(const struct boot98_device *d, const uint8_t *p)
{
	return ((uint32_t)w16(p + 2) * d->heads + p[1]) * d->sectors + p[0];
}

/* PC-98 partition-table discovery using per-device BIOS logical geometry. */
static void devname(int i)
{
	switch (devs[i].device_class) {
	case BOOT98_DEV_FDD:
		puts("fd");
		break;
	case BOOT98_DEV_IDE:
		puts("ide");
		break;
	default:
		puts("scsi");
	}
	dec(devs[i].display_index);
}
static int scanparts(int di)
{
	memzero(parts, sizeof(parts));
	if (di < 0 || !(devs[di].flags & BOOT98_DEV_HAS_GEOMETRY) ||
	    readsec(&devs[di], 1, sec))
		return 0;
	for (int i = 0; i < MAX_PARTS; i++) {
		uint8_t *p = sec + i * 32;
		if (!p[0])
			continue;
		parts[i].valid = 1;
		parts[i].index = i;
		parts[i].bootable = (p[0] & 0x80) && (p[1] & 0x80);
		parts[i].start = chs(&devs[di], p + 4);
		parts[i].data = chs(&devs[di], p + 8);
		for (int j = 0; j < 16; j++) {
			char c = p[16 + j];
			parts[i].name[j] = (c && c != ' ') ? c : 0;
			if (!parts[i].name[j])
				break;
		}
		parts[i].name[16] = 0;
	}
	return 1;
}
static int disk_volume_read(const void *context, uint32_t lba, void *buffer)
{
	return !readsec(context, lba, buffer);
}
static int disk_volume_write(void *context, uint32_t lba,
			     const void *buffer)
{
	return !writesec(context, lba, buffer);
}

static int mountpart_into(struct boot98_filesystem *filesystem,
			  int device_index, int partition_index)
{
	const struct boot98_filesystem_driver *const drivers[] = {
		&boot98_fat16_driver,
	};
	struct boot98_volume volume;

	if (!parts[partition_index].valid)
		return 0;
	/* The shared volume ABI uses mutable context for write callbacks.  The
	 * device descriptor itself remains logically read-only. */
	volume.context = (void *)&devs[device_index];
	volume.start_lba = parts[partition_index].data;
	volume.sector_size = 512;
	volume.read = disk_volume_read;
	volume.write = disk_volume_write;
	return boot98_fs_mount(filesystem, &volume, drivers,
			       sizeof(drivers) / sizeof(drivers[0]));
}

static int mountpart(int device_index, int partition_index)
{
	return mountpart_into(&mounted_fs, device_index, partition_index);
}

static int disk_mount_name(int device_index, char name[8])
{
	unsigned ordinal;

	if (device_index < 0 || (unsigned)device_index >= device_count)
		return 0;
	ordinal = (unsigned)device_index + 1U;
	name[0] = 'd';
	name[1] = 'i';
	name[2] = 's';
	name[3] = 'k';
	if (ordinal < 10U) {
		name[4] = (char)('0' + ordinal);
		name[5] = 0;
	} else {
		name[4] = (char)('0' + ordinal / 10U);
		name[5] = (char)('0' + ordinal % 10U);
		name[6] = 0;
	}
	return 1;
}

static void select_disk_home(int device_index)
{
	char name[8];
	char home[24];
	char dictionary[48];
	unsigned name_length;

	if (!disk_mount_name(device_index, name) ||
	    !boot98_namespace_set_default(&mounted_namespace, name))
		return;
	home[0] = '/';
	name_length = slen(name);
	memcopy(home + 1, name, name_length);
	memcopy(home + 1 + name_length, "/home", 6);
	(void)boot98_env_set(&boot_environment, "HOME", home);
	memcopy(dictionary, home, slen(home));
	memcopy(dictionary + slen(home), "/skkjisyo.dic", 14);
	(void)boot98_env_set(&boot_environment, "REMACS_SKK_DICT", dictionary);
}

/* Mount one user-visible FAT volume per physical disk.  BOOT is preferred;
 * otherwise the first readable FAT16 partition becomes /diskN.  The
 * namespace is intentionally above the filesystem drivers so future ext4
 * and UFS drivers can use the same UNIX path contract. */
static void register_scanned_disk(int device_index)
{
	struct boot98_filesystem filesystem;
	char name[8];
	int preferred = -1;
	int fallback = -1;
	int partition;

	if (!disk_mount_name(device_index, name))
		return;
	for (partition = 0; partition < MAX_PARTS; partition++) {
		if (!parts[partition].valid)
			continue;
		if (streq(parts[partition].name, "BOOT"))
			preferred = partition;
		else if (fallback < 0)
			fallback = partition;
	}
	if (preferred >= 0 &&
	    mountpart_into(&filesystem, device_index, preferred)) {
		(void)boot98_namespace_mount(&mounted_namespace, name, &filesystem);
		return;
	}
	for (partition = fallback; partition >= 0 && partition < MAX_PARTS;
	     partition++)
		if (parts[partition].valid &&
		    mountpart_into(&filesystem, device_index, partition)) {
			(void)boot98_namespace_mount(&mounted_namespace, name,
						&filesystem);
			return;
		}
}

/* Keyboard input, parser, and stateful shell selection helpers. */
static void prompt(void)
{
	if (curdev >= 0)
		devname(curdev);
	else
		puts("none");
	if (curpart >= 0) {
		putc(':');
		puts(parts[curpart].name);
	}
	puts(" ok ");
	update_cursor();
}
static uint32_t raw_key(void)
{
	/* A blocking read must never leave the hardware cursor stale or hidden. */
	update_cursor();
	return call(BOOT98_BIOS_KEY_READ);
}
static int key(void)
{
	return boot98_key_normalize_bios_ax((uint16_t)raw_key());
}
static uint32_t applet_key(void)
{
	return (uint32_t)key();
}
static int poll(void)
{
	return (int)call(BOOT98_BIOS_KEY_POLL);
}

static int noct_key_read(void *context)
{
	(void)context;
	return (int)raw_key();
}

static int noct_key_poll(void *context)
{
	(void)context;
	return poll();
}

/* Return seconds since the start of the current minute, or -1 for an
 * invalid BIOS result.  Stage 1 converts the BIOS BCD byte to binary before
 * returning it through the gateway.  INT 1Ch/AH=00h is available on the
 * early PC-9801 models for which a CPU-speed-dependent delay loop would be
 * least useful. */
static int clock_second(void)
{
	unsigned second = (unsigned)call(BOOT98_BIOS_CLOCK_SECOND) & 0xff;
	if (second > 59)
		return -1;
	return (int)second;
}

static int noct_clock_second(void *context)
{
	(void)context;
	return clock_second();
}

static int line(char *b)
{
	unsigned n = 0;
	for (;;) {
		int k = key();
		if (k == 0x1b) {
			call(BOOT98_BIOS_RETURN_MENU);
			b[0] = 0;
			return -1;
		}
		if (k == '\r' || k == '\n') {
			putc('\n');
			update_cursor();
			b[n] = 0;
			return n;
		}
		if ((k == 8 || k == 0x7f) && n) {
			n--;
			putc('\b');
			update_cursor();
			continue;
		}
		if (k >= 32 && k < 127 && n < LINE_MAX - 1) {
			b[n++] = k;
			putc(k);
			update_cursor();
		}
	}
}
static int split(char *s, char **v, int max)
{
	int n = 0;
	while (*s) {
		while (*s == ' ' || *s == '\t')
			s++;
		if (!*s || *s == '#' || *s == ';')
			break;
		if (n == max)
			break;
		v[n++] = s;
		if (*s == '\"') {
			v[n - 1] = ++s;
			while (*s && *s != '\"')
				s++;
		} else
			while (*s && *s != ' ' && *s != '\t')
				s++;
		if (*s)
			*s++ = 0;
	}
	return n;
}
static int number(const char *s)
{
	int n = 0;
	if (!*s)
		return -1;
	while (*s >= '0' && *s <= '9')
		n = n * 10 + *s++ - '0';
	return *s ? -1 : n;
}

static void listdev(uint8_t cls)
{
	for (unsigned i = 0; i < device_count; i++) {
		if (cls && devs[i].device_class != cls)
			continue;
		devname(i);
		puts(" BIOS ");
		hex8(devs[i].bios_id);
		if (devs[i].flags & BOOT98_DEV_HAS_GEOMETRY) {
			puts(" H/S ");
			dec(devs[i].heads);
			putc('/');
			dec(devs[i].sectors);
		}
		if (devs[i].bios_id == ho->boot_bios_id)
			puts(" boot");
		putc('\n');
	}
}
static int selectdisk(const char *c, const char *n)
{
	int ix = number(n);
	uint8_t cls = streq(c, "fd")     ? 1
	              : streq(c, "ide")  ? 2
	              : streq(c, "scsi") ? 3
	                                 : 0;
	if (!cls || ix < 0)
		return 0;
	for (unsigned i = 0; i < device_count; i++)
		if (devs[i].device_class == cls &&
		    devs[i].display_index == ix) {
			curdev = i;
			curpart = -1;
			kernel_name[0] = kernel_arg[0] = 0;
			scanparts(i);
			return 1;
		}
	return 0;
}
static int selectpart(const char *s)
{
	if (curdev < 0)
		return 0;
	int n = number(s);
	for (int i = 0; i < MAX_PARTS; i++)
		if (parts[i].valid && ((n >= 0 && i == n) ||
		                       (n < 0 && streq(parts[i].name, s)))) {
			if (!mountpart(curdev, i))
				return 0;
			curpart = i;
			select_disk_home(curdev);
			kernel_name[0] = kernel_arg[0] = 0;
			return 1;
		}
	return 0;
}

/* Filesystem-facing shell commands and extension-module loaders. */
static void ls(void)
{
	if (curpart < 0) {
		for (int i = 0; i < MAX_PARTS; i++)
			if (parts[i].valid) {
				dec(i);
				putc(' ');
				puts(parts[i].name);
				puts(" LBA ");
				dec(parts[i].start);
				putc('\n');
		}
		return;
	}
	for (unsigned index = 0;; index++) {
		struct boot98_dirent entry;

		if (!boot98_fs_readdir(&mounted_fs, "", index, &entry))
			return;
		puts(entry.name);
		putc('\n');
	}
}
static int catfile(const char *n)
{
	struct boot98_file file;
	uint64_t offset = 0;

	if (curpart < 0 || !boot98_fs_open(&mounted_fs, n, &file))
		return 0;
	while (offset < file.size) {
		uint32_t k = file.size - offset > 512 ?
		             512 : (uint32_t)(file.size - offset);
		if (!boot98_file_read(&file, offset, sec, k))
			return 0;
		for (uint32_t i = 0; i < k; i++)
			putc(sec[i]);
		offset += k;
	}
	return 1;
}
static int run_iplware(const char *n)
{
	struct boot98_file file;
	uint32_t file_lba;

	if (curpart < 0 || !boot98_fs_open(&mounted_fs, n, &file) ||
	    !file.size || file.size > 0xf600 ||
	    !boot98_file_contiguous_lba(&file, &file_lba))
		return 0;
	if (!boot98_file_read(&file, 0, (void *)0x60100,
			      (uint32_t)file.size))
		return 0;
	unsigned z = slen(n);
	rq.status = (z >= 4 && n[z - 4] == '.' &&
	             (n[z - 3] == 'C' || n[z - 3] == 'c') &&
	             (n[z - 2] == 'O' || n[z - 2] == 'o') &&
	             (n[z - 1] == 'M' || n[z - 1] == 'm'))
	                    ? 2
	                    : 1;
	rq.bios_id = devs[curdev].bios_id;
	rq.lba = file_lba;
	rq.buffer = (uint32_t)file.size;
	if (call(BOOT98_BIOS_IPLWARE))
		return 0;
	findboot();
	call(BOOT98_BIOS_DISPLAY_RESET);
	(void)boot98_beui_pc98_gdc_clear_graphics(&beui_display.gdc);
	boot98_console_reset();
	boot98_console_set_mode(BOOT98_CONSOLE_TERMINAL);
	puts("IPLware returned; devices reprobed.\n");
	return 1;
}
static uint32_t crc32_image(const uint8_t *p, uint32_t n)
{
	uint32_t c = 0xffffffff;
	for (uint32_t i = 0; i < n; i++) {
		uint8_t b = (i >= 16 && i < 20) ? 0 : p[i];
		c ^= b;
		for (int j = 0; j < 8; j++)
			c = (c >> 1) ^ ((0 - (c & 1)) & 0xedb88320);
	}
	return ~c;
}
static int run_applet(const char *n, int argc, char **argv)
{
	struct boot98_file file;
	uint8_t *image = (uint8_t *)0x50000;
	if (curpart < 0 || !boot98_fs_open(&mounted_fs, n, &file) ||
	    file.size < sizeof(struct boot98_applet_header) ||
	    file.size > 0x10000 ||
	    !boot98_file_read(&file, 0, image, (uint32_t)file.size))
		return 0;
	struct boot98_applet_header *h = (struct boot98_applet_header *)image;
	if (h->magic != BOOT98_APPLET_MAGIC || h->abi_version != 1 ||
	    h->header_size != sizeof(*h) || h->image_size != file.size ||
	    h->entry_offset < h->header_size || h->entry_offset >= file.size ||
	    crc32_image(image, (uint32_t)file.size) != h->crc32)
		return 0;
	struct boot98_applet_services s = {1, sizeof(s), putc, puts,
	                                   applet_key};
	boot98_applet_entry_t entry =
	        (boot98_applet_entry_t)(image + h->entry_offset);
	uint32_t r = entry(&s, (uint32_t)argc, (const char *const *)argv);
	if (r) {
		puts("applet status ");
		dec(r);
		putc('\n');
		return 0;
	}
	return 1;
}
static int device_is_known(uint8_t device_class, uint8_t bios_id)
{
	for (unsigned i = 0; i < device_count; i++)
		if (devs[i].device_class == device_class &&
		    devs[i].bios_id == bios_id)
			return 1;
	return 0;
}

static uint8_t bios_workarea_byte(uint32_t address)
{
	uint8_t value;

	asm volatile("movb (%1),%0" : "=q"(value) : "r"(address) : "memory");
	return value;
}

/*
 * 0:0482 is only a target-presence bitmap after a SCSI disk ROM has
 * registered itself.  On machines without a SCSI BIOS the byte is ordinary
 * BIOS work RAM and may contain stale nonzero bits.  Calling INT 1Bh/AH=84h
 * on that basis can enter an absent extension and never return, leaving the
 * keyboard unusable while the startup probe appears stuck at SCSI 8.
 *
 * The NEC disk-ROM registration bytes contain the option-ROM segment high
 * byte.  A SCSI device handed to us by the preceding boot stages is also
 * sufficient evidence: the firmware has already serviced that device.
 */
static int scsi_bios_available(void)
{
	static const uint32_t registration[] = {
		PC98_WA_SCSI_ROM0, PC98_WA_SCSI_ROM1, PC98_WA_SCSI_ROM2,
	};

	for (unsigned i = 0; i < device_count; i++)
		if (devs[i].device_class == BOOT98_DEV_SCSI)
			return 1;
	for (unsigned i = 0; i < sizeof(registration) / sizeof(registration[0]);
	     i++) {
		uint8_t segment_high = bios_workarea_byte(registration[i]);

		if (segment_high != 0x00 && segment_high != 0xff)
			return 1;
	}
	return 0;
}

/*
 * 0:055Dh contains the dense BIOS IDE-drive map in its low nibble.  Unlike
 * physical-slot bitmap 0:05BAh, bit N corresponds directly to BIOS unit
 * 80h+N.  Stock ROMs may not return from SENSE for an absent unit, so this
 * map is authoritative for enumeration.  Preserve a boot unit already
 * handed to Stage 2 even if unusual firmware failed to publish the bit.
 */
static uint8_t ide_reported_drives(void)
{
	uint8_t bitmap = bios_workarea_byte(PC98_WA_IDE_DRIVES) &
			 ((1U << MAX_IDE_DEVICES) - 1U);

	for (unsigned i = 0; i < device_count; i++)
		if (devs[i].device_class == BOOT98_DEV_IDE &&
		    devs[i].bios_id >= 0x80 &&
		    devs[i].bios_id < 0x80 + MAX_IDE_DEVICES)
			bitmap |= 1U << (devs[i].bios_id - 0x80);
	return bitmap;
}

/*
 * A PC-9801-55/92 host adapter normally owns SCSI ID 7.  Some firmware sets
 * bit 7 in 0:0482h for the adapter itself; treating it as an eighth disk and
 * issuing SENSE to A7h can enter firmware that never returns.  Enumerate only
 * the seven target IDs which the registered SCSI BIOS reports as disks.
 */
static uint8_t scsi_reported_targets(void)
{
	uint8_t bitmap;

	if (!scsi_bios_available())
		return 0;
	bitmap = bios_workarea_byte(PC98_WA_SCSI_TARGETS) &
		 ((1U << MAX_SCSI_TARGETS) - 1U);
	for (unsigned i = 0; i < device_count; i++)
		if (devs[i].device_class == BOOT98_DEV_SCSI &&
		    devs[i].bios_id >= 0xa0 &&
		    devs[i].bios_id < 0xa0 + MAX_SCSI_TARGETS)
			bitmap |= 1U << (devs[i].bios_id - 0xa0);
	return bitmap;
}

static unsigned bit_count(uint8_t value)
{
	unsigned count = 0;

	while (value) {
		count += value & 1U;
		value >>= 1;
	}
	return count;
}

/* Probe exactly one BIOS unit.  A nonnegative result is the new list index. */
static int probe_fixed_device(uint8_t device_class, uint8_t bios_id)
{
	uint8_t scsi_bitmap;
	int new_index;

	if (device_count >= MAX_FIXED_DEVICES ||
	    device_is_known(device_class, bios_id))
		return -1;
	if (device_class == BOOT98_DEV_IDE) {
		if (bios_id < 0x80 || bios_id >= 0x80 + MAX_IDE_DEVICES ||
		    !(ide_reported_drives() & (1U << (bios_id - 0x80))))
			return -1;
	} else if (device_class == BOOT98_DEV_SCSI) {
		if (bios_id < 0xa0 || bios_id >= 0xa0 + MAX_SCSI_TARGETS)
			return -1;
		scsi_bitmap = scsi_reported_targets();
		if (!(scsi_bitmap & (1U << (bios_id - 0xa0))))
			return -1;
	}
	new_index = (int)device_count;
	memzero(&discovered_devices[device_count],
	        sizeof(discovered_devices[device_count]));
	rq.status = device_class;
	rq.bios_id = bios_id;
	rq.buffer = (uint32_t)&discovered_devices[device_count];
	if (call(BOOT98_BIOS_PROBE_FIXED) != 0)
		return -1;
	device_count++;
	return new_index;
}

/* Shell probes remain exhaustive; startup uses probe_fixed_device directly. */
static void probe_fixed_class(uint8_t device_class)
{
	uint8_t first = device_class == BOOT98_DEV_IDE ? 0x80 : 0xa0;
	unsigned count = device_class == BOOT98_DEV_IDE ? MAX_IDE_DEVICES :
							MAX_SCSI_TARGETS;

	for (unsigned index = 0; index < count; index++)
		probe_fixed_device(device_class, first + index);
}

/* ELF32 structures used by the uncompressed Linux kernel loader. */
struct eh {
	uint8_t id[16];
	uint16_t type, machine;
	uint32_t ver, entry, phoff, shoff, flags;
	uint16_t ehsize, phsize, phnum;
};
struct ph {
	uint32_t type, off, vaddr, paddr, filesz, memsz, flags, align;
};
static __attribute__((noreturn)) void jump_linux(uint32_t entry)
{
	asm volatile("cli; movb $0xff,%%al; outb %%al,$0x0a; outb %%al,$0x50; "
	             "movl %0,%%esi; xorl %%ebp,%%ebp; xorl %%edi,%%edi; xorl "
	             "%%ebx,%%ebx; jmp *%1" ::"r"(BP_ADDR),
	             "r"(entry)
	             : "eax", "esi", "memory");
	__builtin_unreachable();
}

/*
 * Enable the PC-98 memory mapping required before writing kernel segments
 * above 1 MiB.  The port sequence mirrors the existing Linux loader and must
 * be completed while interrupts are still under Stage 2 control.
 */
static void enable_highmem(void)
{
	asm volatile(
	        "xorb %%al,%%al; outb %%al,$0xf2; movb $2,%%al; outb "
	        "%%al,$0xf6; movw $0x439,%%dx; inb %%dx,%%al; andb $0xfb,%%al; "
	        "outb %%al,%%dx; xorb %%al,%%al; outb %%al,$0xf8; movw "
	        "$0x43b,%%dx; movb $4,%%al; outb %%al,%%dx" ::
	                : "eax", "edx");
}
static uint8_t low8(uint32_t a)
{
	uint8_t v;
	asm volatile("movb (%1),%0" : "=q"(v) : "r"(a));
	return v;
}

static uint16_t low16(uint32_t a)
{
	uint16_t v;
	asm volatile("movw (%1),%0" : "=r"(v) : "r"(a));
	return v;
}

/*
 * Publish one SETUP_PC98_DISK node for every BIOS-visible fixed disk.  Linux
 * needs each disk's own logical geometry to decode its NEC98 partition table;
 * passing only the boot disk is insufficient when IDE and SCSI disks coexist.
 * The device descriptors already contain the Stage 1 SENSE results, so this
 * does not issue more BIOS calls or lengthen the handoff path.
 */
static void build_pc98_disk_setup(uint8_t *bp)
{
	uint8_t *previous = 0;
	unsigned count = 0;

	*(uint32_t *)(bp + 0x250) = 0;
	*(uint32_t *)(bp + 0x254) = 0;
	for (unsigned i = 0; i < device_count; i++) {
		const struct boot98_device *d = &devs[i];
		uint8_t *x;

		if ((d->device_class != BOOT98_DEV_IDE &&
		     d->device_class != BOOT98_DEV_SCSI) ||
		    !(d->flags & BOOT98_DEV_HAS_GEOMETRY) ||
		    !d->heads || !d->sectors || count >= 12)
			continue;

		x = (uint8_t *)(PC98_ADDR + count * PC98_SETUP_NODE_SIZE);
		memzero(x, PC98_SETUP_NODE_SIZE);
		if (!count)
			*(uint32_t *)(bp + 0x250) = (uint32_t)x;
		if (previous)
			*(uint32_t *)(previous + 0) = (uint32_t)x;
		*(uint32_t *)(x + 8) = 11;
		*(uint32_t *)(x + 12) = 12;
		*(uint32_t *)(x + 16) = 0x44383950;
		*(uint16_t *)(x + 20) = 1;
		*(uint16_t *)(x + 22) = 12;
		x[24] = d->bios_id;
		x[25] = d->heads;
		x[26] = d->sectors;
		if ((int)i == curdev)
			x[27] = BOOT98_LINUX_DISK_F_BOOT;
		previous = x;
		count++;
	}
}

/*
 * Load every PT_LOAD segment, construct Linux boot_params and the PC-98
 * extension block, then enter the ELF entry point.  BIOS logical H/S and the
 * original BIOS drive number are preserved for the kernel partition parser.
 */
static int vmlinux_probe(struct boot98_file *file)
{
	struct eh e;

	return file->size <= 0xffffffffU &&
	       boot98_file_read(file, 0, &e, sizeof(e)) &&
	       w32(e.id) == 0x464c457f && e.id[4] == 1 && e.id[5] == 1 &&
	       e.machine == 3;
}

static void load_progress(void *context, uint32_t bytes)
{
	int load_class = *(const int *)context;

	if (load_class)
		load_data_done += bytes;
	else
		load_text_done += bytes;
	show_load_progress(load_class);
}

static int vmlinux_load(struct boot98_file *file, const char *arguments)
{
	struct eh e;
	struct ph p;
	unsigned load_segments = 0;

	if (!boot98_file_read(file, 0, &e, sizeof(e)))
		return 0;
	if (w32(e.id) != 0x464c457f || e.id[4] != 1 || e.id[5] != 1 ||
	    e.machine != 3 || e.phsize != sizeof(p) || e.phnum > 16)
		return 0;
	load_text_done = load_text_total = 0;
	load_data_done = load_data_total = 0;
	for (unsigned i = 0; i < e.phnum; i++) {
		if (!boot98_file_read(file, e.phoff + i * sizeof(p), &p,
				      sizeof(p)))
			return 0;
		if (p.type != 1)
			continue;
		if (p.filesz > p.memsz || p.paddr < 0x100000 ||
		    p.off + p.filesz < p.off || p.off + p.filesz > file->size)
			return 0;
		if (p.flags & 2) {
			if (load_data_total + p.filesz < load_data_total)
				return 0;
			load_data_total += p.filesz;
		} else {
			if (load_text_total + p.filesz < load_text_total)
				return 0;
			load_text_total += p.filesz;
		}
		load_segments++;
	}
	if (!load_segments)
		return 0;
	begin_load_progress((uint32_t)file->size);
	enable_highmem();
	for (unsigned i = 0; i < e.phnum; i++) {
		int load_class;

		if (!boot98_file_read(file, e.phoff + i * sizeof(p), &p,
				      sizeof(p)))
			return 0;
		if (p.type != 1)
			continue;
		if (p.filesz > p.memsz || p.paddr < 0x100000 ||
		    p.off + p.filesz > file->size)
			return 0;
		load_class = !!(p.flags & 2);
		if (!boot98_file_read_progress(file, p.off, (void *)p.paddr,
					       p.filesz, load_progress,
					       &load_class))
			return 0;
		memzero((void *)(p.paddr + p.filesz), p.memsz - p.filesz);
	}
	memzero((void *)BP_ADDR, 4096);
	memcopy((void *)CMD_ADDR, arguments, slen(arguments) + 1);
	uint8_t *bp = (uint8_t *)BP_ADDR;
	*(uint32_t *)(bp + 0x228) = CMD_ADDR;
	bp[0x210] = 0xff;
	bp[0x1e8] = 2;
	build_pc98_disk_setup(bp);
	uint32_t conv = ((low8(0x501) & 7) + 1) << 17;
	uint32_t ext = low8(0x401) << 17;
	uint8_t *em = bp + 0x2d0;
	*(uint64_t *)(em + 0) = 0;
	*(uint64_t *)(em + 8) = conv;
	*(uint32_t *)(em + 16) = 1;
	*(uint64_t *)(em + 20) = 0x100000;
	*(uint64_t *)(em + 28) = ext;
	*(uint32_t *)(em + 36) = 1;
	/*
	 * 0:0594h reports the number of MiB above the PC-98 16 MiB boundary.
	 * The first C implementation accidentally omitted this third E820 entry,
	 * limiting a 64 MiB machine to the memory described by 0:0401 (about
	 * 16 MiB) and causing severe swap thrashing during Debian login.
	 */
	uint16_t high_mib = low16(0x594);
	if (high_mib) {
		*(uint64_t *)(em + 40) = 0x1000000;
		*(uint64_t *)(em + 48) = (uint64_t)high_mib << 20;
		*(uint32_t *)(em + 56) = 1;
		bp[0x1e8] = 3;
	} else {
		bp[0x1e8] = 2;
	}
	jump_linux(e.entry);
}

static const struct boot98_image_loader vmlinux_loader = {
	"vmlinux", vmlinux_probe, vmlinux_load
};

static int linuxboot(void)
{
	if (curpart < 0 || !kernel_name[0])
		return 0;
	return boot98_image_boot(&vmlinux_loader, &mounted_fs, kernel_name,
				 kernel_arg);
}

#ifdef BOOT98_M9_WRITE_TEST
static void m9_debug_puts(const char *text)
{
	while (*text) {
		uint8_t character = (uint8_t)*text++;

		asm volatile("outb %0,$0xe9" : : "a"(character));
	}
}

static void m9_report(const char *text)
{
	puts(text);
	m9_debug_puts(text);
}

static int m9_same_sector(const uint8_t *left, const uint8_t *right)
{
	for (unsigned i = 0; i < 512; i++)
		if (left[i] != right[i])
			return 0;
	return 1;
}

/* Destructive raw-sector test, compiled only into BOOT-M9.SYS.  The caller
 * must select an expendable sector in a temporary image.  Once the first
 * write succeeds, every exit path attempts to restore the original sector. */
static int m9_write_test(uint32_t lba)
{
	int result = 1;

	if (curdev < 0) {
		m9_report("M9 BIOS write test: no selected disk\n");
		return 0;
	}
	if (readsec(&devs[curdev], lba, m9_original)) {
		m9_report("M9 BIOS write test: initial read failed\n");
		return 0;
	}
	for (unsigned i = 0; i < sizeof(m9_pattern); i++)
		m9_pattern[i] = (uint8_t)(0xa5U ^ i ^ lba ^ (lba >> 8));
	if (writesec(&devs[curdev], lba, m9_pattern)) {
		m9_report("M9 BIOS write test: pattern write failed\n");
		return 0;
	}
	if (readsec(&devs[curdev], lba, m9_observed) ||
	    !m9_same_sector(m9_observed, m9_pattern)) {
		m9_report("M9 BIOS write test: pattern read-back failed\n");
		result = 0;
	}
	if (writesec(&devs[curdev], lba, m9_original)) {
		m9_report("M9 BIOS write test: RESTORE FAILED\n");
		return 0;
	}
	if (readsec(&devs[curdev], lba, m9_observed) ||
	    !m9_same_sector(m9_observed, m9_original)) {
		m9_report("M9 BIOS write test: restore read-back failed\n");
		return 0;
	}
	if (result)
		m9_report("M9 BIOS write/read/restore: PASS\n");
	return result;
}
#endif

/* Execute one already-tokenized shell command against the current state. */
static int run_noct_application(const char *name, const char *extension,
				int argc, char *const argv[])
{
	char path[BOOT98_PATH_MAX];
	struct boot98_file file;
	unsigned base_length = 0;
	unsigned extension_length = 0;
	unsigned position = 4;

	path[0] = 'C';
	path[1] = 'M';
	path[2] = 'D';
	path[3] = '/';
	while (name[base_length] != '\0') {
		char ch = name[base_length++];

		if (ch == '.' || ch == '/' || ch == '\\' ||
		    position + 1U >= sizeof(path))
			return 0;
		path[position++] = ch >= 'a' && ch <= 'z' ?
			(char)(ch - 'a' + 'A') : ch;
	}
	if (!base_length)
		return 0;
	while (extension[extension_length] != '\0') {
		if (position + 1U >= sizeof(path))
			return 0;
		path[position++] = extension[extension_length++];
	}
	path[position] = '\0';
	if (!boot98_fs_open(&mounted_fs, path, &file)) {
		/* Preserve compatibility with pre-CMD BOOT volumes. */
		for (unsigned index = 4; index <= position; index++)
			path[index - 4] = path[index];
		if (!boot98_fs_open(&mounted_fs, path, &file))
			return 0;
	}
	return boot98_noct_run_file(&mounted_namespace, &mounted_fs,
				    &boot_environment, path,
				    argc, argv, noct_key_read, noct_key_poll,
				    noct_clock_second, 0);
}

static int command(char *s)
{
	char *v[20];
	int n = split(s, v, 20);
	if (!n)
		return 1;
#ifdef BOOT98_M9_WRITE_TEST
	if (streq(v[0], "m9-write-test")) {
		int lba = n == 2 ? number(v[1]) : -1;

		return lba >= 0 && m9_write_test((uint32_t)lba);
	}
#endif
	if (streq(v[0], "help")) {
		puts("help echo env set unset pause wait devalias probe-ide probe-scsi "
		     "disk part ls cat source kernel arg boot linux "
		     "run iplware noct emacs noct-test reboot halt\n");
		return 1;
	}
	if (streq(v[0], "env")) {
		if (n != 1)
			return 0;
		for (size_t index = 0;
		     index < boot98_env_count(&boot_environment); index++) {
			const char *name;
			const char *value;

			if (!boot98_env_at(&boot_environment, index, &name, &value))
				return 0;
			puts(name);
			putc('=');
			puts(value);
			putc('\n');
		}
		return 1;
	}
	if (streq(v[0], "set"))
		return n == 3 &&
		       boot98_env_set(&boot_environment, v[1], v[2]);
	if (streq(v[0], "unset")) {
		if (n != 2 || !boot98_env_name_valid(v[1]))
			return 0;
		(void)boot98_env_unset(&boot_environment, v[1]);
		return 1;
	}
	if (streq(v[0], "echo")) {
		for (int i = 1; i < n; i++) {
			if (i > 1)
				putc(' ');
			puts(v[i]);
		}
		putc('\n');
		return 1;
	}
	if (streq(v[0], "pause")) {
		for (int i = 1; i < n; i++) {
			puts(v[i]);
			putc(' ');
		}
		key();
		return 1;
	}
	if (streq(v[0], "wait")) {
		unsigned loops = n > 1 ? (unsigned)number(v[1]) * 50000 : 50000;
		while (loops-- && poll() < 0)
			;
		return 1;
	}
	if (streq(v[0], "devalias")) {
		listdev(0);
		puts("boot -> BIOS ");
		hex8(ho->boot_bios_id);
		putc('\n');
		return 1;
	}
	if (streq(v[0], "probe-ide")) {
		probe_fixed_class(BOOT98_DEV_IDE);
		listdev(BOOT98_DEV_IDE);
		return 1;
	}
	if (streq(v[0], "probe-scsi")) {
		probe_fixed_class(BOOT98_DEV_SCSI);
		listdev(BOOT98_DEV_SCSI);
		return 1;
	}
	if (streq(v[0], "disk")) {
		if (n == 1) {
			if (curdev >= 0)
				devname(curdev);
			putc('\n');
			return 1;
		}
		return n == 3 && selectdisk(v[1], v[2]);
	}
	if (streq(v[0], "part")) {
		if (n == 1) {
			ls();
			return 1;
		}
		return selectpart(v[1]);
	}
	if (streq(v[0], "ls")) {
		ls();
		return 1;
	}
	if (streq(v[0], "cat"))
		return n == 2 && catfile(v[1]);
	if (streq(v[0], "kernel")) {
		if (n == 1) {
			puts(kernel_name);
			putc('\n');
			return 1;
		}
		if (!strcopy(kernel_name, v[1], sizeof(kernel_name)))
			return 0;
		kernel_arg[0] = 0;
		return 1;
	}
	if (streq(v[0], "arg")) {
		kernel_arg[0] = 0;
		unsigned z = 0;
		for (int i = 1; i < n; i++) {
			if (i > 1 && z < 255)
				kernel_arg[z++] = ' ';
			for (char *p = v[i]; *p && z < 255;)
				kernel_arg[z++] = *p++;
		}
		kernel_arg[z] = 0;
		return 1;
	}
	if (streq(v[0], "linux")) {
		if (n < 2)
			return 0;
		if (!strcopy(kernel_name, v[1], sizeof(kernel_name)))
			return 0;
		kernel_arg[0] = 0;
		unsigned z = 0;
		for (int i = 2; i < n; i++) {
			if (i > 2)
				kernel_arg[z++] = ' ';
			for (char *p = v[i]; *p && z < 255;)
				kernel_arg[z++] = *p++;
		}
		kernel_arg[z] = 0;
		return linuxboot();
	}
	if (streq(v[0], "boot")) {
		if (kernel_name[0])
			return linuxboot();
		if (curdev < 0)
			return 0;
		rq.status = curpart >= 0 ? 1 : 0;
		rq.bios_id = devs[curdev].bios_id;
		rq.heads = devs[curdev].heads;
		rq.sectors = devs[curdev].sectors;
		rq.lba = curpart >= 0 ? parts[curpart].start : 0;
		return call(BOOT98_BIOS_CHAIN_BOOT) == 0;
	}
	if (streq(v[0], "source")) {
		struct boot98_file file;

		if (n != 2 || !boot98_fs_open(&mounted_fs, v[1], &file) ||
		    file.size >= CFG_MAX ||
		    !boot98_file_read(&file, 0, cfg, (uint32_t)file.size))
			return 0;
		cfg[(uint32_t)file.size] = 0;
		char *p = (char *)cfg;
		unsigned ln = 1;
		while (*p) {
			char *q = p;
			while (*q && *q != '\n' && *q != '\r')
				q++;
			char save = *q;
			*q = 0;
			if (!command(p)) {
				puts("source error line ");
				dec(ln);
				putc('\n');
				return 0;
			}
			*q = save;
			while (*q == '\n' || *q == '\r')
				q++, ln++;
			p = q;
		}
		return 1;
	}
	if (streq(v[0], "halt")) {
		for (;;)
			asm volatile("cli; hlt");
	}
	if (streq(v[0], "reboot")) {
		asm volatile("movb $0x0f,%%al; outb %%al,$0x37" ::: "eax");
		for (;;)
			;
	}
	if (streq(v[0], "iplware"))
		return n == 2 && run_iplware(v[1]);
	if (streq(v[0], "run"))
		return n >= 2 && run_applet(v[1], n - 2, &v[2]);
	if (streq(v[0], "noct")) {
		if (n == 1)
			return boot98_noct_run_repl(&mounted_namespace, &mounted_fs,
						    &boot_environment, noct_key_read,
						    noct_key_poll,
						    noct_clock_second, 0);
		return boot98_noct_run_file(&mounted_namespace, &mounted_fs,
					    &boot_environment,
					    v[1], n - 2, &v[2],
					    noct_key_read, noct_key_poll,
					    noct_clock_second, 0);
	}
	if (streq(v[0], "emacs")) {
		const char *dictionary = boot98_env_get(&boot_environment,
						      "REMACS_SKK_DICT");

		/* The 8.3 path is present in every Boots image with REMACS.NAP. */
		if (dictionary == NULL || dictionary[0] == '\0')
			(void)boot98_env_set(&boot_environment, "REMACS_SKK_DICT",
					     "HOME/SKKJISYO.DIC");
		return run_noct_application("REMACS", ".NAP", n - 1, &v[1]);
	}
	if (streq(v[0], "noct-test")) {
		int repeat;

		if (n > 2)
			return 0;
		repeat = n == 2 ? number(v[1]) : 1;
		if (repeat < 1 || repeat > 100)
			return 0;
		return boot98_noct_run_embedded((unsigned)repeat);
	}
	/* Unknown unqualified names resolve to NAME.NCT on the selected BOOT
	 * filesystem.  C built-ins above always retain precedence, including
	 * their argument-validation failures. */
	return run_noct_application(v[0], ".NCT", n - 1, &v[1]);
}

/* Prefer a BOOT partition on the original boot device, then scan the rest. */
static void findboot(void)
{
	for (unsigned pass = 0; pass < 2; pass++)
		for (unsigned i = 0; i < device_count; i++) {
			if (!(devs[i].flags & BOOT98_DEV_HAS_GEOMETRY))
				continue;
			if ((pass == 0) !=
			    (devs[i].bios_id == ho->boot_bios_id))
				continue;
			if (!scanparts(i))
				continue;
			for (int p = 0; p < MAX_PARTS; p++)
				if (parts[p].valid &&
				    streq(parts[p].name, "BOOT") &&
				    mountpart(i, p)) {
					curdev = i;
					curpart = p;
					select_disk_home((int)i);
					return;
				}
		}
}

/* The startup menu exposes only the first four fixed disks. The full stable
 * discovery order remains addressable through devalias and disk. */
static int menu_device(unsigned ordinal)
{
	unsigned found = 0;
	for (unsigned i = 0; i < device_count; i++) {
		if (++found == ordinal)
			return (int)i;
	}
	return -1;
}

static enum startup_config_kind boot_volume_startup_kind(void)
{
	struct boot98_file file;

	if (boot98_fs_open(&mounted_fs, "AUTOEXEC.NCT", &file))
		return STARTUP_CONFIG_AUTOEXEC;
	if (boot98_fs_open(&mounted_fs, "BOOT.CFG", &file))
		return STARTUP_CONFIG_BOOTCFG;
	return STARTUP_CONFIG_NONE;
}

static unsigned fixed_device_ordinal(int device)
{
	unsigned ordinal = 0;

	for (int i = 0; i <= device; i++)
		ordinal++;
	return ordinal;
}

static void consider_automatic_device(struct startup_state *state, int device)
{
	int first_bootable = -1;
	int config_partition = -1;
	enum startup_config_kind config_kind = STARTUP_CONFIG_NONE;

	if (device < 0 || !(devs[device].flags & BOOT98_DEV_HAS_GEOMETRY) ||
	    !scanparts(device))
		return;
	register_scanned_disk(device);
	for (int partition = 0; partition < MAX_PARTS; partition++) {
		if (!parts[partition].valid)
			continue;
		/* The BOOT volume's PBR reloads this loader.  It must not be the
		 * fallback target when BOOT.CFG is absent, or Auto loops forever. */
		if (first_bootable < 0 && parts[partition].bootable &&
		    !streq(parts[partition].name, "BOOT"))
			first_bootable = partition;
		if (config_partition < 0 && streq(parts[partition].name, "BOOT") &&
		    mountpart(device, partition) &&
		    (config_kind = boot_volume_startup_kind()) !=
			    STARTUP_CONFIG_NONE)
			config_partition = partition;
	}
	if (config_partition >= 0 && state->auto_priority > 1) {
		/* Discovery order is stable: keep the first BOOT volume found. */
		state->auto_priority = 1;
		state->auto_kind = STARTUP_AUTO_CONFIG;
		state->auto_config_kind = config_kind;
		state->auto_device = device;
		state->auto_partition = config_partition;
	}
	if (first_bootable >= 0 && 3 < state->auto_priority) {
		state->auto_priority = 3;
		state->auto_kind = STARTUP_AUTO_PBR;
		state->auto_device = device;
		state->auto_partition = first_bootable;
	}
}

static int activate_automatic_target(const struct startup_state *state)
{
	if (state->auto_kind == STARTUP_AUTO_NONE ||
	    !scanparts(state->auto_device) ||
	    !parts[state->auto_partition].valid)
		return 0;
	if (state->auto_kind == STARTUP_AUTO_CONFIG &&
	    (!mountpart(state->auto_device, state->auto_partition) ||
	     boot_volume_startup_kind() == STARTUP_CONFIG_NONE))
		return 0;
	curdev = state->auto_device;
	curpart = state->auto_partition;
	select_disk_home(curdev);
	kernel_name[0] = kernel_arg[0] = 0;
	return 1;
}

/* AUTOEXEC.NCT may select one action, but it cannot inject a second shell
 * line or leave a stale action behind for a later VM invocation. */
static int valid_boot_action(const char *action)
{
	unsigned length = 0;
	int non_space = 0;

	if (action == 0)
		return 0;
	while (action[length] != 0) {
		unsigned char ch = (unsigned char)action[length++];

		if (length >= LINE_MAX || ch < 0x20U || ch == 0x7fU)
			return 0;
		if (ch != ' ' && ch != '\t')
			non_space = 1;
	}
	return non_space;
}

/* Return zero when no graphical startup script exists, one after executing
 * its selected action, and -1 when the script/action failed validation. */
static int run_autoexec(void)
{
	struct boot98_file file;
	const char *selected;
	char action[LINE_MAX];
	int script_ok;

	if (!boot98_fs_open(&mounted_fs, "AUTOEXEC.NCT", &file))
		return 0;
	(void)boot98_env_unset(&boot_environment, "BOOT_ACTION");
	script_ok = boot98_noct_run_file(&mounted_namespace, &mounted_fs,
					 &boot_environment,
					 "AUTOEXEC.NCT", 0, 0, noct_key_read,
					 noct_key_poll, noct_clock_second, 0);
	/* A graphical script may have owned Cirrus or GDC graphics.  Restore the
	 * firmware text display and erase every GDC graphics plane before its
	 * selected Boots command runs.  Real Cirrus-equipped machines retain the
	 * old graphics VRAM contents when the display is switched back to GDC. */
	call(BOOT98_BIOS_DISPLAY_RESET);
	(void)boot98_beui_pc98_gdc_clear_graphics(&beui_display.gdc);
	boot98_console_reset();
	boot98_console_set_mode(BOOT98_CONSOLE_TERMINAL);
	if (!script_ok) {
		puts("AUTOEXEC.NCT failed; returning to the text shell.\n");
		return -1;
	}
	selected = boot98_env_get(&boot_environment, "BOOT_ACTION");
	if (!valid_boot_action(selected) ||
	    !strcopy(action, selected, sizeof(action))) {
		(void)boot98_env_unset(&boot_environment, "BOOT_ACTION");
		puts("AUTOEXEC.NCT did not select a valid BOOT_ACTION.\n");
		return -1;
	}
	(void)boot98_env_unset(&boot_environment, "BOOT_ACTION");
	if (!command(action)) {
		puts("BOOT_ACTION failed: ");
		puts(action);
		putc('\n');
		return -1;
	}
	return 1;
}

static void draw_startup_header(void)
{
	boot98_console_write_at(0, 0, boot98_msg_machine);
	boot98_console_write_at(2, 0, boot98_msg_loader);
	boot98_console_write_at(3, 0, boot98_msg_copyright);
	boot98_console_write_at(5, 0, boot98_msg_probing);
}

static void draw_probe_bar(unsigned current, unsigned total)
{
	char filled[BOOT98_CONSOLE_COLUMNS + 1U];
	char empty[BOOT98_CONSOLE_COLUMNS + 1U];
	unsigned columns = total ? current * BOOT98_CONSOLE_COLUMNS / total : 0;
	unsigned index;

	if (columns > BOOT98_CONSOLE_COLUMNS)
		columns = BOOT98_CONSOLE_COLUMNS;
	for (index = 0; index < columns; index++)
		filled[index] = ' ';
	filled[index] = 0;
	for (index = 0; index < BOOT98_CONSOLE_COLUMNS - columns; index++)
		empty[index] = ' ';
	empty[index] = 0;
	boot98_console_put_sjis_at(BOOT98_CONSOLE_ROWS - 1U, 0,
				   (const uint8_t *)filled,
				   BOOT98_CONSOLE_NORMAL_ATTRIBUTE | 0x04U);
	if (columns < BOOT98_CONSOLE_COLUMNS)
		boot98_console_put_sjis_at(BOOT98_CONSOLE_ROWS - 1U, columns,
					   (const uint8_t *)empty,
					   BOOT98_CONSOLE_NORMAL_ATTRIBUTE);
}

static void draw_probe_progress(unsigned current, unsigned total,
				uint8_t device_class, uint8_t bios_id)
{

	boot98_console_clear_row(5);
	boot98_console_write_at(5, 0, boot98_msg_probing);
	putc(' ');
	puts(device_class == BOOT98_DEV_IDE ? "IDE " : "SCSI ");
	dec((unsigned)bios_id -
	    (device_class == BOOT98_DEV_IDE ? 0x80U : 0xa0U) + 1U);
	puts(" (");
	dec(current);
	putc('/');
	dec(total);
	putc(')');
	draw_probe_bar(current, total);
}

static void draw_automatic_status(const struct startup_state *state)
{
	draw_probe_bar(state->probe_total, state->probe_total);
	boot98_console_clear_row(5);
	boot98_console_write_at(5, 0, boot98_msg_automatic_run);
	if (state->auto_config_kind == STARTUP_CONFIG_AUTOEXEC)
		puts(" AUTOEXEC.NCT");
	else if (state->auto_config_kind == STARTUP_CONFIG_BOOTCFG)
		puts(" BOOT.CFG");
}

static void draw_startup_menu(const struct startup_state *state)
{
	for (unsigned menu_row = 6; menu_row <= 17; menu_row++)
		boot98_console_clear_row(menu_row);
	boot98_console_write_at(6, 0, (const uint8_t *)"");
	dec(state->fixed_count);
	puts((const char *)boot98_msg_found_suffix);
	boot98_console_write_at(8, 0, boot98_msg_boot_from);
	boot98_console_write_at(9, 0, boot98_msg_auto_prefix);
	if (state->phase == STARTUP_DRAW || state->phase == STARTUP_PROBE) {
		puts((const char *)boot98_msg_searching);
	} else if (state->auto_kind != STARTUP_AUTO_NONE) {
		puts("HDD ");
		dec(fixed_device_ordinal(state->auto_device));
		puts((const char *)boot98_msg_partition);
		dec((unsigned)state->auto_partition + 1);
		if (state->auto_kind == STARTUP_AUTO_CONFIG) {
			if (state->auto_config_kind == STARTUP_CONFIG_AUTOEXEC)
				puts((const char *)boot98_msg_run_autoexec);
			else
				puts((const char *)boot98_msg_run_cfg);
		}
	} else {
		puts((const char *)boot98_msg_unavailable);
	}
	putc(')');

	for (unsigned ordinal = 1; ordinal <= 4; ordinal++) {
		if (menu_device(ordinal) < 0)
			continue;
		boot98_console_write_at(9 + ordinal, 0,
					(const uint8_t *)"  ");
		putc((char)('1' + ordinal));
		puts((const char *)boot98_msg_fixed_disk_prefix);
		dec(ordinal);
	}
	boot98_console_write_at(15, 0, boot98_msg_esc_shell);
	boot98_console_write_at(17, 0, boot98_msg_select);
	update_cursor();
}

static void accept_startup_selection(int key_code)
{
	boot98_console_clear_row(BOOT98_CONSOLE_ROWS - 1U);
	if (key_code == 0x1b)
		puts("ESC");
	else if (key_code >= 0)
		putc((char)key_code);
	putc('\n');
	boot98_console_set_mode(BOOT98_CONSOLE_TERMINAL);
}

static void chain_menu_device(unsigned ordinal)
{
	int di = menu_device(ordinal);
	if (di < 0) {
		puts("Device is not present.\n");
		return;
	}
	curdev = di;
	curpart = -1;
	kernel_name[0] = kernel_arg[0] = 0;
	rq.status = 0;
	rq.bios_id = devs[di].bios_id;
	rq.heads = devs[di].heads;
	rq.sectors = devs[di].sectors;
	rq.lba = 0;
	if (call(BOOT98_BIOS_CHAIN_BOOT) != 0)
		puts("Boot failed.\n");
}

static void chain_automatic_partition(const struct startup_state *state)
{
	if (!activate_automatic_target(state)) {
		puts("Automatic target is no longer readable.\n");
		return;
	}
	rq.status = 1;
	rq.bios_id = devs[curdev].bios_id;
	rq.heads = devs[curdev].heads;
	rq.sectors = devs[curdev].sectors;
	rq.lba = parts[curpart].start;
	if (call(BOOT98_BIOS_CHAIN_BOOT) != 0)
		puts("Boot failed.\n");
}

/* Return -1 for an ignored key, zero for Shell, and one for BOOT.CFG. */
static int handle_startup_key(struct startup_state *state, int key_code)
{
	if (key_code == 0x1b) {
		accept_startup_selection(key_code);
		state->phase = STARTUP_SHELL;
		if (state->auto_kind == STARTUP_AUTO_CONFIG)
			activate_automatic_target(state);
		return 0;
	}
	if (key_code == '1') {
		if (state->auto_kind == STARTUP_AUTO_NONE)
			return -1;
		accept_startup_selection(key_code);
		state->phase = STARTUP_SELECTED;
		if (state->auto_kind == STARTUP_AUTO_CONFIG)
			return activate_automatic_target(state) ? 1 : 0;
		chain_automatic_partition(state);
		state->phase = STARTUP_SHELL;
		return 0;
	}
	if (key_code >= '2' && key_code <= '5') {
		unsigned ordinal = (unsigned)(key_code - '1');

		if (menu_device(ordinal) < 0)
			return -1;
		accept_startup_selection(key_code);
		state->phase = STARTUP_SELECTED;
		chain_menu_device(ordinal);
		state->phase = STARTUP_SHELL;
		return 0;
	}
	return -1;
}

static int pending_startup_key(void)
{
	return poll() >= 0 ? key() : -1;
}

/* Process one stable candidate; at most one invocation reaches INT 1Bh. */
static void probe_next_startup_device(struct startup_state *state)
{
	unsigned candidate;
	uint8_t device_class;
	uint8_t bios_id;
	int new_device;

	for (;;) {
		if (state->next_candidate >= MAX_FIXED_DEVICES)
			return;
		candidate = state->next_candidate++;
		if (candidate < MAX_IDE_DEVICES) {
			device_class = BOOT98_DEV_IDE;
			bios_id = 0x80 + candidate;
			if (state->ide_bitmap & (1U << candidate))
				break;
			continue;
		}
		device_class = BOOT98_DEV_SCSI;
		bios_id = 0xa0 + candidate - MAX_IDE_DEVICES;
		if (state->scsi_bitmap &
		    (1U << (candidate - MAX_IDE_DEVICES)))
			break;
	}
	state->probe_done++;
	draw_probe_progress(state->probe_done, state->probe_total,
			    device_class, bios_id);
	new_device = probe_fixed_device(device_class, bios_id);
	state->fixed_count = device_count;
	if (new_device >= 0)
		consider_automatic_device(state, new_device);
}

/* Explicit cooperative startup state machine.  BIOS SENSE itself may block,
 * but keyboard input is checked immediately before and after every candidate. */
static int startup_menu(struct startup_state *state)
{
	curdev = curpart = -1;
	boot98_namespace_init(&mounted_namespace);
	state->phase = STARTUP_DRAW;
	state->next_candidate = 0;
	state->ide_bitmap = ide_reported_drives();
	state->scsi_bitmap = scsi_reported_targets();
	state->probe_total = bit_count(state->ide_bitmap) +
			     bit_count(state->scsi_bitmap);
	state->probe_done = 0;
	state->fixed_count = device_count;
	state->auto_device = state->auto_partition = -1;
	state->auto_priority = 4;
	state->auto_kind = STARTUP_AUTO_NONE;
	state->auto_config_kind = STARTUP_CONFIG_NONE;
	state->automatic_cancelled = 0;
	state->timeout_start = -1;
	state->timeout_budget = 0x20000;

	boot98_console_reset();
	draw_startup_header();
	for (unsigned device = 0; device < device_count; device++)
		consider_automatic_device(state, device);
	draw_startup_menu(state);
	state->phase = STARTUP_PROBE;
	for (;;) {
		int key_code;
		int result;

		if (state->phase == STARTUP_PROBE) {
			key_code = pending_startup_key();
			if (key_code >= 0) {
				state->automatic_cancelled = 1;
				if ((result = handle_startup_key(state, key_code)) >= 0)
					return result;
			}
			if (state->next_candidate < MAX_FIXED_DEVICES) {
				probe_next_startup_device(state);
				draw_startup_menu(state);
				key_code = pending_startup_key();
				if (key_code >= 0) {
					state->automatic_cancelled = 1;
					if ((result = handle_startup_key(state,
								 key_code)) >= 0)
						return result;
				}
				continue;
			}
			state->phase = STARTUP_TIMEOUT;
			state->timeout_start = clock_second();
			if (!state->automatic_cancelled &&
			    state->auto_kind != STARTUP_AUTO_NONE)
				draw_automatic_status(state);
			draw_startup_menu(state);
			continue;
		}

		if (state->auto_kind == STARTUP_AUTO_NONE ||
		    state->automatic_cancelled) {
			key_code = key();
			result = handle_startup_key(state, key_code);
			if (result >= 0)
				return result;
			continue;
		}

		key_code = pending_startup_key();
		if (key_code >= 0) {
			state->automatic_cancelled = 1;
			if ((result = handle_startup_key(state, key_code)) >= 0)
				return result;
			continue;
		}
		int now = clock_second();
		if ((state->timeout_start >= 0 && now >= 0 &&
		     (now - state->timeout_start + 60) % 60 >=
		     STARTUP_TIMEOUT_SECONDS) || !--state->timeout_budget) {
			accept_startup_selection(-1);
			state->phase = STARTUP_SELECTED;
			if (state->auto_kind == STARTUP_AUTO_CONFIG)
				return activate_automatic_target(state) ? 1 : 0;
			chain_automatic_partition(state);
			state->phase = STARTUP_SHELL;
			return 0;
		}
	}
}

/* Validated Stage 1 handoff and top-level Stage 2 command loop. */
void boot98_main(const struct boot98_handoff *h)
{
	char b[LINE_MAX];
	ho = h;
	if (!h || h->magic != BOOT98_HANDOFF_MAGIC || h->version != 1 ||
	    h->size < sizeof(*h) || !h->device_count || !h->device_table ||
	    !h->bios_gateway)
		for (;;)
			asm volatile("cli; hlt");
	device_count = 0;
	const struct boot98_device *initial =
		(const struct boot98_device *)h->device_table;
	for (unsigned i = 0; i < h->device_count &&
	     device_count < MAX_FIXED_DEVICES; i++) {
		if ((initial[i].device_class != BOOT98_DEV_IDE &&
		     initial[i].device_class != BOOT98_DEV_SCSI) ||
		    !(initial[i].flags & BOOT98_DEV_PRESENT))
			continue;
		discovered_devices[device_count++] = initial[i];
	}
	devs = discovered_devices;
	gw = (boot98_bios_gateway_t)h->bios_gateway;
	boot98_beui_pc98_auto_default(&beui_display, beui_display_reset,
				      beui_display_stop, NULL);
	if (boot98_beui_pc98_auto_make_hal(&beui_hal, &beui_display))
		boot98_noct_set_beui_hal(&beui_hal);
	boot98_env_init(&boot_environment);
	(void)boot98_env_set(&boot_environment, "HOME", "/");
	(void)boot98_env_set(&boot_environment, "REMACS_SKK_DICT",
			     "/skkjisyo.dic");
	for (;;) {
		struct startup_state startup;

		call(BOOT98_BIOS_DISPLAY_RESET);
		int automatic = startup_menu(&startup);
		if (curpart >= 0) {
			puts("source: ");
			devname(curdev);
			putc(':');
			puts(parts[curpart].name);
			putc('\n');
		}
		if (automatic) {
			int autoexec = curpart >= 0 ? run_autoexec() : -1;

			if (autoexec == 0) {
				char source_cfg[] = "source BOOT.CFG";

				if (!command(source_cfg))
					puts("BOOT.CFG automatic boot failed.\n");
			}
		}
		for (;;) {
			prompt();
			if (line(b) < 0)
				break;
			if (!command(b))
				puts("error\n");
		}
	}
}
