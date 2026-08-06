/*
 * PC-9800 Bootloader
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "boot98-abi.h"
#include "boot98-console.h"
#include "boot98-fat16.h"
#include "boot98-fs.h"
#include "boot98-image.h"
#include "boot98-messages.h"

#define MAX_PARTS 16
#define CFG_MAX 8192
#define LINE_MAX 256
#define BP_ADDR 0x70000U
#define CMD_ADDR 0x71000U
#define PC98_ADDR 0x72000U
#define PC98_SETUP_NODE_SIZE 32U
#define STARTUP_TIMEOUT_SECONDS 3
#define MAX_FIXED_DEVICES 12

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

struct startup_state {
	enum startup_phase phase;
	unsigned next_candidate;
	unsigned fixed_count;
	int auto_device;
	int auto_partition;
	int auto_priority;
	enum startup_auto_kind auto_kind;
	int timeout_start;
	unsigned timeout_budget;
};
static struct boot98_filesystem mounted_fs;
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
static int readsec(const struct boot98_device *d, uint32_t lba, void *buf)
{
	rq.bios_id = d->bios_id;
	rq.heads = d->heads;
	rq.sectors = d->sectors;
	rq.lba = lba;
	rq.buffer = (uint32_t)buf;
	return call(BOOT98_BIOS_DISK_READ) != 0;
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

static int mountpart(int device_index, int partition_index)
{
	const struct boot98_filesystem_driver *const drivers[] = {
		&boot98_fat16_driver,
	};
	struct boot98_volume volume;

	if (!parts[partition_index].valid)
		return 0;
	volume.context = &devs[device_index];
	volume.start_lba = parts[partition_index].data;
	volume.sector_size = 512;
	volume.read = disk_volume_read;
	return boot98_fs_mount(&mounted_fs, &volume, drivers,
			       sizeof(drivers) / sizeof(drivers[0]));
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
static int key(void)
{
	/* A blocking read must never leave the hardware cursor stale or hidden. */
	update_cursor();
	return (int)call(BOOT98_BIOS_KEY_READ);
}
static uint32_t applet_key(void)
{
	return (uint32_t)key();
}
static int poll(void)
{
	return (int)call(BOOT98_BIOS_KEY_POLL);
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

/* Probe exactly one BIOS unit.  A nonnegative result is the new list index. */
static int probe_fixed_device(uint8_t device_class, uint8_t bios_id)
{
	uint8_t scsi_bitmap;
	int new_index;

	if (device_count >= MAX_FIXED_DEVICES ||
	    device_is_known(device_class, bios_id))
		return -1;
	if (device_class == BOOT98_DEV_SCSI) {
		asm volatile("movb 0x0482,%0" : "=q"(scsi_bitmap));
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
	unsigned count = device_class == BOOT98_DEV_IDE ? 4 : 8;

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

/* Execute one already-tokenized shell command against the current state. */
static int command(char *s)
{
	char *v[20];
	int n = split(s, v, 20);
	if (!n)
		return 1;
	if (streq(v[0], "help")) {
		puts("help echo pause wait devalias probe-ide probe-scsi "
		     "disk part ls cat source kernel arg boot linux "
		     "run iplware reboot halt\n");
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
	return 0;
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

static unsigned fixed_device_ordinal(int device)
{
	unsigned ordinal = 0;

	for (int i = 0; i <= device; i++)
		ordinal++;
	return ordinal;
}

static void consider_automatic_device(struct startup_state *state, int device)
{
	struct boot98_file file;
	int first_bootable = -1;
	int config_partition = -1;
	int priority;

	if (device < 0 || !(devs[device].flags & BOOT98_DEV_HAS_GEOMETRY) ||
	    !scanparts(device))
		return;
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
		    boot98_fs_open(&mounted_fs, "BOOT.CFG", &file))
			config_partition = partition;
	}
	if (config_partition >= 0) {
		priority = devs[device].bios_id == ho->boot_bios_id ? 1 : 2;
		if (priority < state->auto_priority) {
			state->auto_priority = priority;
			state->auto_kind = STARTUP_AUTO_CONFIG;
			state->auto_device = device;
			state->auto_partition = config_partition;
		}
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
	struct boot98_file file;

	if (state->auto_kind == STARTUP_AUTO_NONE ||
	    !scanparts(state->auto_device) ||
	    !parts[state->auto_partition].valid)
		return 0;
	if (state->auto_kind == STARTUP_AUTO_CONFIG &&
	    (!mountpart(state->auto_device, state->auto_partition) ||
	     !boot98_fs_open(&mounted_fs, "BOOT.CFG", &file)))
		return 0;
	curdev = state->auto_device;
	curpart = state->auto_partition;
	kernel_name[0] = kernel_arg[0] = 0;
	return 1;
}

static void draw_startup_header(void)
{
	boot98_console_write_at(0, 0, boot98_msg_machine);
	boot98_console_write_at(2, 0, boot98_msg_loader);
	boot98_console_write_at(3, 0, boot98_msg_copyright);
	boot98_console_write_at(5, 0, boot98_msg_probing);
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
		if (state->auto_kind == STARTUP_AUTO_CONFIG)
			puts((const char *)boot98_msg_run_cfg);
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
	unsigned candidate = state->next_candidate++;
	uint8_t device_class;
	uint8_t bios_id;
	int new_device;

	if (candidate < 4) {
		device_class = BOOT98_DEV_IDE;
		bios_id = 0x80 + candidate;
	} else {
		device_class = BOOT98_DEV_SCSI;
		bios_id = 0xa0 + candidate - 4;
	}
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
	state->phase = STARTUP_DRAW;
	state->next_candidate = 0;
	state->fixed_count = device_count;
	state->auto_device = state->auto_partition = -1;
	state->auto_priority = 4;
	state->auto_kind = STARTUP_AUTO_NONE;
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
			if (key_code >= 0 &&
			    (result = handle_startup_key(state, key_code)) >= 0)
				return result;
			if (state->next_candidate < MAX_FIXED_DEVICES) {
				probe_next_startup_device(state);
				draw_startup_menu(state);
				key_code = pending_startup_key();
				if (key_code >= 0 &&
				    (result = handle_startup_key(state,
							 key_code)) >= 0)
					return result;
				continue;
			}
			state->phase = STARTUP_TIMEOUT;
			state->timeout_start = clock_second();
			draw_startup_menu(state);
			continue;
		}

		if (state->auto_kind == STARTUP_AUTO_NONE) {
			key_code = key();
			result = handle_startup_key(state, key_code);
			if (result >= 0)
				return result;
			continue;
		}

		key_code = pending_startup_key();
		if (key_code >= 0 &&
		    (result = handle_startup_key(state, key_code)) >= 0)
			return result;
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
			char source_cfg[] = "source BOOT.CFG";
			if (curpart < 0 || !command(source_cfg))
				puts("BOOT.CFG automatic boot failed.\n");
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
