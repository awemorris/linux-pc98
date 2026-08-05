/*
 * PC-9800 Bootloader
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "boot98-abi.h"

#define MAX_PARTS 16
#define CFG_MAX 8192
#define LINE_MAX 256
#define BP_ADDR 0x70000U
#define CMD_ADDR 0x71000U
#define PC98_ADDR 0x72000U

/*
 * Stage 2 runs without a C library or operating-system services.  Text and
 * attribute VRAM are therefore accessed directly.  The request object is the
 * sole mutable argument passed through the real-mode BIOS gateway in Stage 1.
 */
static volatile uint16_t *const tv = (volatile uint16_t *)0xa0000;
static volatile uint8_t *const av = (volatile uint8_t *)0xa2000;
static boot98_bios_gateway_t gw;
static const struct boot98_handoff *ho;
static const struct boot98_device *devs;
static struct boot98_bios_request rq;
static uint8_t sec[512], cfg[CFG_MAX];
static unsigned row = 13, col;

struct part {
	uint8_t valid, index;
	char name[17];
	uint32_t start, data;
};
struct fat {
	const struct boot98_device *d;
	uint32_t part, fat, root, data;
	uint16_t roots;
	uint8_t spc;
} fs;
struct dent {
	char name[12];
	uint16_t cluster;
	uint32_t size;
};
static struct part parts[MAX_PARTS];
static int curdev = -1, curpart = -1;
static char kernel_name[12], kernel_arg[256];
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

/* Lower-half-screen console used by the interactive command shell. */
static void clear_lower(void)
{
	unsigned p;
	for (p = 13 * 80; p < 25 * 80; p++) {
		tv[p] = ' ';
		av[p * 2] = 0xe1;
	}
	row = 13;
	col = 0;
}
static void nl(void)
{
	col = 0;
	if (++row < 25)
		return;
	for (unsigned r = 13; r < 24; r++)
		for (unsigned c = 0; c < 80; c++) {
			tv[r * 80 + c] = tv[(r + 1) * 80 + c];
			av[(r * 80 + c) * 2] = av[((r + 1) * 80 + c) * 2];
		}
	for (unsigned c = 0; c < 80; c++) {
		tv[24 * 80 + c] = ' ';
		av[(24 * 80 + c) * 2] = 0xe1;
	}
	row = 24;
}
static void putc(char c)
{
	if (c == '\n') {
		nl();
		return;
	}
	if (c == '\r')
		return;
	if (c == '\b') {
		if (col) {
			--col;
			tv[row * 80 + col] = ' ';
		}
		return;
	}
	if (col >= 80)
		nl();
	tv[row * 80 + col] = (uint8_t)c;
	av[(row * 80 + col) * 2] = 0xe1;
	col++;
}
static void puts(const char *s)
{
	while (*s)
		putc(*s++);
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
static int mountpart(int di, int pi)
{
	uint8_t scale, nf;
	uint16_t reserved, fatsz, roots;
	if (!parts[pi].valid || readsec(&devs[di], parts[pi].data, sec))
		return 0;
	if (w16(sec + 11) == 512)
		scale = 1;
	else if (w16(sec + 11) == 1024)
		scale = 2;
	else
		return 0;
	fs.d = &devs[di];
	fs.part = parts[pi].data;
	fs.spc = sec[13] * scale;
	reserved = w16(sec + 14) * scale;
	nf = sec[16];
	roots = w16(sec + 17);
	fatsz = w16(sec + 22) * scale;
	if (!fs.spc || !nf || !fatsz)
		return 0;
	fs.fat = fs.part + reserved;
	fs.root = fs.fat + (uint32_t)nf * fatsz;
	fs.roots = roots;
	fs.data = fs.root + (((uint32_t)roots * 32 + 511) >> 9);
	return 1;
}

/* Read-only FAT16 support.  Stage 2 deliberately performs no FAT writes. */
static void fatname(const char *in, char out[11])
{
	int i = 0, j = 0;
	for (; j < 11; j++)
		out[j] = ' ';
	while (*in && *in != '.' && i < 8) {
		char c = *in++;
		out[i++] = (c >= 'a' && c <= 'z') ? c - 32 : c;
	}
	if (*in == '.')
		in++;
	i = 8;
	while (*in && i < 11) {
		char c = *in++;
		out[i++] = (c >= 'a' && c <= 'z') ? c - 32 : c;
	}
}
static int findfile(const char *name, struct dent *d)
{
	char n[11];
	fatname(name, n);
	for (uint32_t e = 0; e < fs.roots; e++) {
		if (!(e & 15) && readsec(fs.d, fs.root + (e >> 4), sec))
			return 0;
		uint8_t *p = sec + (e & 15) * 32;
		if (!p[0])
			return 0;
		if (p[0] == 0xe5 || p[11] == 0x0f || p[11] & 0x18)
			continue;
		int ok = 1;
		for (int j = 0; j < 11; j++)
			if (p[j] != (uint8_t)n[j])
				ok = 0;
		if (ok) {
			for (int j = 0; j < 11; j++)
				d->name[j] = n[j];
			d->name[11] = 0;
			d->cluster = w16(p + 26);
			d->size = w32(p + 28);
			return d->cluster >= 2;
		}
	}
	return 0;
}
static uint16_t nextcl(uint16_t c)
{
	uint32_t off = (uint32_t)c * 2;
	if (readsec(fs.d, fs.fat + (off >> 9), sec))
		return 0xffff;
	return w16(sec + (off & 511));
}
static int contiguous(const struct dent *d)
{
	uint16_t c = d->cluster;
	uint32_t left = d->size;
	while (left > (uint32_t)fs.spc * 512) {
		uint16_t n = nextcl(c);
		if (n != (uint16_t)(c + 1))
			return 0;
		c = n;
		left -= (uint32_t)fs.spc * 512;
	}
	return 1;
}
static int file_read(const struct dent *d, uint32_t off, void *dst, uint32_t n)
{
	uint16_t c = d->cluster;
	uint32_t skip = off / 512, within = off & 511;
	while (skip >= fs.spc) {
		c = nextcl(c);
		if (c >= 0xfff8)
			return 0;
		skip -= fs.spc;
	}
	uint8_t *out = dst;
	while (n) {
		uint32_t l = fs.data + (uint32_t)(c - 2) * fs.spc + skip;
		if (readsec(fs.d, l, sec))
			return 0;
		uint32_t k = 512 - within;
		if (k > n)
			k = n;
		memcopy(out, sec + within, k);
		out += k;
		n -= k;
		within = 0;
		if (++skip >= fs.spc && n) {
			skip = 0;
			c = nextcl(c);
			if (c >= 0xfff8)
				return 0;
		}
	}
	return 1;
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
}
static int key(void)
{
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
static int line(char *b)
{
	unsigned n = 0;
	for (;;) {
		int k = key();
		if (k == 0x1b) {
			call(BOOT98_BIOS_RETURN_MENU);
			for (;;)
				;
		}
		if (k == '\r' || k == '\n') {
			putc('\n');
			b[n] = 0;
			return n;
		}
		if ((k == 8 || k == 0x7f) && n) {
			n--;
			putc('\b');
			continue;
		}
		if (k >= 32 && k < 127 && n < LINE_MAX - 1) {
			b[n++] = k;
			putc(k);
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
	for (unsigned i = 0; i < ho->device_count; i++) {
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
	for (unsigned i = 0; i < ho->device_count; i++)
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
	for (uint32_t e = 0; e < fs.roots; e++) {
		if (!(e & 15) && readsec(fs.d, fs.root + (e >> 4), sec))
			return;
		uint8_t *p = sec + (e & 15) * 32;
		if (!p[0])
			return;
		if (p[0] == 0xe5 || p[11] == 0x0f)
			continue;
		for (int j = 0; j < 8 && p[j] != ' '; j++)
			putc(p[j]);
		if (p[8] != ' ') {
			putc('.');
			for (int j = 8; j < 11 && p[j] != ' '; j++)
				putc(p[j]);
		}
		putc('\n');
	}
}
static int catfile(const char *n)
{
	struct dent d;
	if (curpart < 0 || !findfile(n, &d))
		return 0;
	uint32_t off = 0;
	while (off < d.size) {
		uint32_t k = d.size - off;
		if (k > 512)
			k = 512;
		if (!file_read(&d, off, sec, k))
			return 0;
		for (uint32_t i = 0; i < k; i++)
			putc(sec[i]);
		off += k;
	}
	return 1;
}
static int run_iplware(const char *n)
{
	struct dent d;
	if (curpart < 0 || !findfile(n, &d) || !d.size || d.size > 0xf600 ||
	    !contiguous(&d))
		return 0;
	if (!file_read(&d, 0, (void *)0x60100, d.size))
		return 0;
	unsigned z = slen(n);
	rq.status = (z >= 4 && n[z - 4] == '.' &&
	             (n[z - 3] == 'C' || n[z - 3] == 'c') &&
	             (n[z - 2] == 'O' || n[z - 2] == 'o') &&
	             (n[z - 1] == 'M' || n[z - 1] == 'm'))
	                    ? 2
	                    : 1;
	rq.bios_id = devs[curdev].bios_id;
	rq.lba = fs.data + (uint32_t)(d.cluster - 2) * fs.spc;
	rq.buffer = d.size;
	if (call(BOOT98_BIOS_IPLWARE))
		return 0;
	findboot();
	call(BOOT98_BIOS_DISPLAY_RESET);
	clear_lower();
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
	struct dent d;
	uint8_t *image = (uint8_t *)0x50000;
	if (curpart < 0 || !findfile(n, &d) ||
	    d.size < sizeof(struct boot98_applet_header) || d.size > 0x10000 ||
	    !file_read(&d, 0, image, d.size))
		return 0;
	struct boot98_applet_header *h = (struct boot98_applet_header *)image;
	if (h->magic != BOOT98_APPLET_MAGIC || h->abi_version != 1 ||
	    h->header_size != sizeof(*h) || h->image_size != d.size ||
	    h->entry_offset < h->header_size || h->entry_offset >= d.size ||
	    crc32_image(image, d.size) != h->crc32)
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
static void reprobe(void)
{
	call(BOOT98_BIOS_REPROBE);
	curdev = curpart = -1;
	kernel_name[0] = kernel_arg[0] = 0;
	findboot();
	call(BOOT98_BIOS_DISPLAY_RESET);
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

/*
 * Load every PT_LOAD segment, construct Linux boot_params and the PC-98
 * extension block, then enter the ELF entry point.  BIOS logical H/S and the
 * original BIOS drive number are preserved for the kernel partition parser.
 */
static int linuxboot(void)
{
	struct dent d;
	struct eh e;
	struct ph p;
	if (curpart < 0 || !kernel_name[0] || !findfile(kernel_name, &d) ||
	    !file_read(&d, 0, &e, sizeof(e)))
		return 0;
	if (w32(e.id) != 0x464c457f || e.id[4] != 1 || e.id[5] != 1 ||
	    e.machine != 3 || e.phsize != sizeof(p) || e.phnum > 16)
		return 0;
	enable_highmem();
	for (unsigned i = 0; i < e.phnum; i++) {
		if (!file_read(&d, e.phoff + i * sizeof(p), &p, sizeof(p)))
			return 0;
		if (p.type != 1)
			continue;
		if (p.filesz > p.memsz || p.paddr < 0x100000 ||
		    p.off + p.filesz > d.size)
			return 0;
		if (!file_read(&d, p.off, (void *)p.paddr, p.filesz))
			return 0;
		memzero((void *)(p.paddr + p.filesz), p.memsz - p.filesz);
	}
	memzero((void *)BP_ADDR, 4096);
	memcopy((void *)CMD_ADDR, kernel_arg, slen(kernel_arg) + 1);
	uint8_t *bp = (uint8_t *)BP_ADDR;
	*(uint32_t *)(bp + 0x228) = CMD_ADDR;
	bp[0x210] = 0xff;
	bp[0x1e8] = 2;
	*(uint32_t *)(bp + 0x250) = PC98_ADDR;
	uint8_t *x = (uint8_t *)PC98_ADDR;
	memzero(x, 32);
	*(uint32_t *)(x + 8) = 11;
	*(uint32_t *)(x + 12) = 12;
	*(uint32_t *)(x + 16) = 0x44383950;
	*(uint16_t *)(x + 20) = 1;
	*(uint16_t *)(x + 22) = 12;
	x[24] = devs[curdev].bios_id;
	x[25] = devs[curdev].heads;
	x[26] = devs[curdev].sectors;
	uint32_t conv = ((low8(0x501) & 7) + 1) << 17;
	uint32_t ext = low8(0x401) << 17;
	uint8_t *em = bp + 0x2d0;
	*(uint64_t *)(em + 0) = 0;
	*(uint64_t *)(em + 8) = conv;
	*(uint32_t *)(em + 16) = 1;
	*(uint64_t *)(em + 20) = 0x100000;
	*(uint64_t *)(em + 28) = ext;
	*(uint32_t *)(em + 36) = 1;
	bp[0x1e8] = 2;
	jump_linux(e.entry);
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
		     "probe-fd disk part ls cat source kernel arg boot linux "
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
		reprobe();
		listdev(2);
		return 1;
	}
	if (streq(v[0], "probe-scsi")) {
		reprobe();
		listdev(3);
		return 1;
	}
	if (streq(v[0], "probe-fd")) {
		reprobe();
		listdev(1);
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
		fatname(v[1], kernel_name);
		kernel_name[11] = 0;
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
		fatname(v[1], kernel_name);
		kernel_name[11] = 0;
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
		rq.lba = curpart >= 0 ? parts[curpart].start : 0;
		return call(BOOT98_BIOS_CHAIN_BOOT) == 0;
	}
	if (streq(v[0], "source")) {
		struct dent d;
		if (n != 2 || !findfile(v[1], &d) || d.size >= CFG_MAX ||
		    !file_read(&d, 0, cfg, d.size))
			return 0;
		cfg[d.size] = 0;
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
		for (unsigned i = 0; i < ho->device_count; i++) {
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

/* Fixed third-stage menu. FDD numbering is separate from the combined
 * fixed-disk order, so IDE and SCSI media can both appear as HDD 1/2. */
static int menu_device(int floppy, unsigned ordinal)
{
	unsigned found = 0;
	for (unsigned i = 0; i < ho->device_count; i++) {
		int is_floppy = devs[i].device_class == BOOT98_DEV_FDD;
		if (is_floppy != floppy)
			continue;
		if (++found == ordinal)
			return (int)i;
	}
	return -1;
}

static void chain_menu_device(int floppy, unsigned ordinal)
{
	int di = menu_device(floppy, ordinal);
	if (di < 0) {
		puts("Device is not present.\n");
		return;
	}
	curdev = di;
	curpart = -1;
	kernel_name[0] = kernel_arg[0] = 0;
	rq.status = 0;
	rq.bios_id = devs[di].bios_id;
	rq.lba = 0;
	if (call(BOOT98_BIOS_CHAIN_BOOT) != 0)
		puts("Boot failed.\n");
}

/* Return one for Auto and zero for an interactive Shell. Successful chain
 * boots do not return through the BIOS gateway. */
static int startup_menu(void)
{
	for (;;) {
		clear_lower();
		puts("BOOT98\n\n"
		     "1) Auto\n"
		     "2) FDD 1\n"
		     "3) FDD 2\n"
		     "4) HDD 1\n"
		     "5) HDD 2\n"
		     "6) Shell\n\n"
		     "Select: ");
		int k = key();
		putc((char)k);
		putc('\n');
		switch (k) {
		case '1':
			return 1;
		case '2':
			chain_menu_device(1, 1);
			break;
		case '3':
			chain_menu_device(1, 2);
			break;
		case '4':
			chain_menu_device(0, 1);
			break;
		case '5':
			chain_menu_device(0, 2);
			break;
		case '6':
			return 0;
		default:
			continue;
		}
		puts("Press any key to return to the menu.\n");
		key();
	}
}

/* Validated Stage 1 handoff and top-level Stage 2 command loop. */
void boot98_main(const struct boot98_handoff *h)
{
	char b[LINE_MAX];
	ho = h;
	if (!h || h->magic != BOOT98_HANDOFF_MAGIC)
		for (;;)
			asm volatile("cli; hlt");
	devs = (const struct boot98_device *)h->device_table;
	gw = (boot98_bios_gateway_t)h->bios_gateway;
	findboot();
	call(BOOT98_BIOS_DISPLAY_RESET);
	int automatic = startup_menu();
	clear_lower();
	puts("BOOT98 Stage 3 (32-bit C)\n");
	if (curpart >= 0) {
		puts("source: ");
		devname(curdev);
		putc(':');
		puts(parts[curpart].name);
		putc('\n');
	}
	if (automatic) {
		char source_cfg[] = "source BOOT98.CFG";
		if (curpart < 0 || !command(source_cfg))
			puts("BOOT98.CFG automatic boot failed.\n");
	}
	for (;;) {
		prompt();
		line(b);
		if (!command(b))
			puts("error\n");
	}
}
