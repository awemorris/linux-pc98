/*
 * PC-98 Linux ELF loader for DOS.
 *
 * Derived from elfboot, Copyright (C) 2005, 2026, Awe Morris.
 * This program is intended for real-mode DOS.  It loads an uncompressed
 * Linux vmlinux through DOS file I/O and enters the 32-bit kernel directly.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dos.h>
#include <i86.h>
#include <conio.h>

#define ET_EXEC                 2
#define EM_386                  3
#define PT_LOAD                 1
#define SEG_BUF_SIZE            4096
#define BOOT_PARAMS_SIZE        4096
#define SETUP_PC98_DISK         11
#define PC98_BOOT_DISK_MAGIC    0x44383950UL
#define PC98_BOOT_DISK_VERSION  1
#define PC98_BOOT_DISK_F_BOOT   0x02
#define MAX_PC98_DISKS          12

typedef unsigned long u32;
typedef unsigned short u16;
typedef unsigned char u8;

#pragma pack(push, 1)
struct elf_header {
	u8 ident[16];
	u16 type;
	u16 machine;
	u32 version;
	u32 entry;
	u32 phoff;
	u32 shoff;
	u32 flags;
	u16 ehsize;
	u16 phentsize;
	u16 phnum;
	u16 shentsize;
	u16 shnum;
	u16 shstrndx;
};

struct program_header {
	u32 type;
	u32 offset;
	u32 vaddr;
	u32 paddr;
	u32 filesz;
	u32 memsz;
	u32 flags;
	u32 align;
};

struct setup_data_pc98 {
	u32 next_lo;
	u32 next_hi;
	u32 type;
	u32 len;
	u32 magic;
	u16 version;
	u16 size;
	u8 bios_drive;
	u8 heads;
	u8 sectors;
	u8 flags;
};
#pragma pack(pop)

static u8 gdt[24] = {
	0, 0, 0, 0, 0, 0, 0, 0,
	0xff, 0xff, 0, 0, 0, 0x9a, 0xcf, 0,
	0xff, 0xff, 0, 0, 0, 0x92, 0xcf, 0,
};
static u8 gdtr[6];
static u8 seg_buf[SEG_BUF_SIZE];
static u8 boot_params[BOOT_PARAMS_SIZE];
static char command_line[128];
static struct setup_data_pc98 pc98_data[MAX_PC98_DISKS];
static unsigned pc98_data_count;
static struct elf_header ehdr;
static FILE *kernel_file;
static u32 kernel_entry;
static u8 bios_drive;
static u8 bios_heads;
static u8 bios_sectors;

static u32 linear_address(void far *p)
{
	return ((u32)FP_SEG(p) << 4) + FP_OFF(p);
}

static void enable_a20(void)
{
	u8 far *lo = (u8 far *)MK_FP(0, 0);
	u8 far *hi = (u8 far *)MK_FP(0xffff, 0x10);
	u8 save = *lo;

	outp(0xf2, 0);
	while (1) {
		if (++*lo == *hi)
			continue;
		if (++*lo == *hi)
			continue;
		break;
	}
	*lo = save;
}

static void linear_memcpy(u32 src_addr, u32 dst_addr, u32 bytes)
{
	u32 count = bytes;
	u16 gdtr_addr = FP_OFF((void far *)gdtr);

	enable_a20();
	__asm {
		pushf
		cli
		push ds
		push es
		push ax
		push cx
		push si
		push di
		mov bx, gdtr_addr
		db 0fh
		db 01h
		db 17h                 /* lgdt [bx] */
		mov eax, cr0
		or al, 1
		mov cr0, eax
		jmp copy_pm
	copy_pm:
		mov ax, 10h
		mov ds, ax
		mov es, ax
		mov esi, src_addr
		mov edi, dst_addr
		mov ecx, count
	copy_bytes:
		mov al, [esi]
		mov [edi], al
		inc esi
		inc edi
		dec ecx
		jnz copy_bytes
		mov eax, cr0
		and al, 0feh
		mov cr0, eax
		jmp copy_rm
	copy_rm:
		pop di
		pop si
		pop cx
		pop ax
		pop es
		pop ds
		popf
	}
}

static int read_program_header(unsigned index, struct program_header *phdr)
{
	u32 offset = ehdr.phoff + (u32)index * ehdr.phentsize;

	if (fseek(kernel_file, offset, SEEK_SET) ||
	    fread(phdr, sizeof(*phdr), 1, kernel_file) != 1)
		return -1;
	return 0;
}

static int load_segment(const struct program_header *phdr)
{
	u32 left, dest, source;

	if (phdr->filesz > phdr->memsz || phdr->paddr < 0x100000UL ||
	    phdr->paddr + phdr->memsz < phdr->paddr)
		return -1;
	if (fseek(kernel_file, phdr->offset, SEEK_SET))
		return -1;

	source = linear_address((void far *)seg_buf);
	dest = phdr->paddr;
	left = phdr->filesz;
	while (left) {
		u16 chunk = left > SEG_BUF_SIZE ? SEG_BUF_SIZE : (u16)left;
		if (fread(seg_buf, 1, chunk, kernel_file) != chunk)
			return -1;
		linear_memcpy(source, dest, chunk);
		dest += chunk;
		left -= chunk;
		putchar('.');
	}

	memset(seg_buf, 0, sizeof(seg_buf));
	left = phdr->memsz - phdr->filesz;
	while (left) {
		u16 chunk = left > SEG_BUF_SIZE ? SEG_BUF_SIZE : (u16)left;
		linear_memcpy(source, dest, chunk);
		dest += chunk;
		left -= chunk;
	}
	return 0;
}

static int load_kernel(const char *path)
{
	struct program_header phdr;
	unsigned i, loaded = 0;
	u8 magic[7] = { 0x7f, 'E', 'L', 'F', 1, 1, 1 };

	kernel_file = fopen(path, "rb");
	if (!kernel_file) {
		printf("Cannot open %s\n", path);
		return -1;
	}
	if (fread(&ehdr, sizeof(ehdr), 1, kernel_file) != 1 ||
	    memcmp(ehdr.ident, magic, sizeof(magic)) ||
	    ehdr.type != ET_EXEC || ehdr.machine != EM_386 ||
	    ehdr.phentsize != sizeof(phdr) || !ehdr.phnum || ehdr.phnum > 64) {
		printf("Invalid ELF32/i386 executable\n");
		fclose(kernel_file);
		return -1;
	}

	printf("Entry %08lX, %u program headers\n", ehdr.entry, ehdr.phnum);
	for (i = 0; i < ehdr.phnum; i++) {
		if (read_program_header(i, &phdr))
			goto failed;
		if (phdr.type != PT_LOAD)
			continue;
		printf("LOAD %08lX %08lX/%08lX ", phdr.paddr,
		       phdr.filesz, phdr.memsz);
		if (load_segment(&phdr))
			goto failed;
		printf(" ok\n");
		loaded++;
	}
	fclose(kernel_file);
	if (!loaded)
		return -1;
	kernel_entry = ehdr.entry;
	return 0;

failed:
	printf("\nKernel read or segment validation failed\n");
	fclose(kernel_file);
	return -1;
}

static int sense_disk(unsigned requested_drive, u8 *heads, u8 *sectors)
{
	union REGS inregs, outregs;

	memset(&inregs, 0, sizeof(inregs));
	inregs.h.ah = 0x84;
	inregs.h.al = requested_drive;
	inregs.x.bx = 0x0100;
	int86(0x1b, &inregs, &outregs);
	if (outregs.x.cflag || outregs.x.bx != 512 ||
	    !outregs.h.dh || !outregs.h.dl)
		return -1;
	*heads = outregs.h.dh;
	*sectors = outregs.h.dl;
	return 0;
}

static void remember_disk(u8 drive, u8 heads, u8 sectors, u8 flags)
{
	struct setup_data_pc98 *data;
	unsigned i;

	for (i = 0; i < pc98_data_count; i++) {
		if (pc98_data[i].bios_drive == drive) {
			pc98_data[i].heads = heads;
			pc98_data[i].sectors = sectors;
			pc98_data[i].flags = flags;
			return;
		}
	}
	if (pc98_data_count >= MAX_PC98_DISKS)
		return;
	data = &pc98_data[pc98_data_count++];
	memset(data, 0, sizeof(*data));
	data->type = SETUP_PC98_DISK;
	data->len = 12;
	data->magic = PC98_BOOT_DISK_MAGIC;
	data->version = PC98_BOOT_DISK_VERSION;
	data->size = 12;
	data->bios_drive = drive;
	data->heads = heads;
	data->sectors = sectors;
	data->flags = flags;
}

/*
 * Pass every BIOS-visible fixed disk to Linux.  NEC98 partition entries use
 * BIOS logical CHS, which can differ per controller and per disk.  The boot
 * drive is appended last and marked explicitly; failed SENSE calls simply
 * mean that the corresponding BIOS unit is absent.
 */
static void probe_disks(void)
{
	u8 heads, sectors, drive;
	unsigned unit;

	pc98_data_count = 0;
	printf("Probing BIOS fixed disks...\n");
	for (unit = 0; unit < 4; unit++) {
		drive = 0x80 + unit;
		if (drive == bios_drive || sense_disk(drive, &heads, &sectors))
			continue;
		remember_disk(drive, heads, sectors, 0);
		printf("  %02Xh: */%u/%u\n", drive, heads, sectors);
	}
	for (unit = 0; unit < 8; unit++) {
		drive = 0xa0 + unit;
		if (drive == bios_drive || sense_disk(drive, &heads, &sectors))
			continue;
		remember_disk(drive, heads, sectors, 0);
		printf("  %02Xh: */%u/%u\n", drive, heads, sectors);
	}
	remember_disk(bios_drive, bios_heads, bios_sectors,
		      PC98_BOOT_DISK_F_BOOT);
	printf("  %02Xh: */%u/%u (boot)\n", bios_drive, bios_heads,
	       bios_sectors);
}

static int drive_prefix(const char *text, char a, char b, char c)
{
	return (text[0] | 0x20) == a && (text[1] | 0x20) == b &&
	       (text[2] | 0x20) == c;
}

static int parse_bios_drive(const char *text, unsigned *drive)
{
	size_t len = strlen(text);

	if (len == 1 && text[0] >= '0' && text[0] <= '3') {
		*drive = 0x80 + text[0] - '0';
		return 0;
	}
	if (len == 4 && drive_prefix(text, 'i', 'd', 'e') &&
	    text[3] >= '0' && text[3] <= '3') {
		*drive = 0x80 + text[3] - '0';
		return 0;
	}
	if (len == 5 && (text[0] | 0x20) == 's' &&
	    (text[1] | 0x20) == 'c' &&
	    (text[2] | 0x20) == 's' && (text[3] | 0x20) == 'i' &&
	    text[4] >= '0' && text[4] <= '7') {
		*drive = 0xa0 + text[4] - '0';
		return 0;
	}
	return -1;
}

static void put_e820(u16 offset, u32 addr, u32 size)
{
	*(u32 *)&boot_params[offset] = addr;
	*(u32 *)&boot_params[offset + 4] = 0;
	*(u32 *)&boot_params[offset + 8] = size;
	*(u32 *)&boot_params[offset + 12] = 0;
	*(u32 *)&boot_params[offset + 16] = 1;
}

static void build_boot_params(void)
{
	u8 far *b501 = (u8 far *)MK_FP(0, 0x501);
	u8 far *b401 = (u8 far *)MK_FP(0, 0x401);
	u16 far *w594 = (u16 far *)MK_FP(0, 0x594);
	u32 low_size = ((u32)(*b501 & 7) + 1) << 17;
	u32 high_size = (u32)*b401 << 17;
	u32 top_size = (u32)*w594 << 20;
	u32 command_addr = linear_address((void far *)command_line);
	u32 setup_addr = pc98_data_count ?
		linear_address((void far *)&pc98_data[0]) : 0;
	u8 entries = 2;
	unsigned i;

	memset(boot_params, 0, sizeof(boot_params));
	boot_params[0x210] = 0xff;
	*(u32 *)&boot_params[0x228] = command_addr;
	*(u32 *)&boot_params[0x250] = setup_addr;
	*(u32 *)&boot_params[0x254] = 0;
	put_e820(0x2d0, 0, low_size);
	put_e820(0x2e4, 0x100000UL, high_size);
	if (top_size) {
		put_e820(0x2f8, 0x1000000UL, top_size);
		entries = 3;
	}
	boot_params[0x1e8] = entries;

	for (i = 0; i < pc98_data_count; i++) {
		if (i + 1 < pc98_data_count)
			pc98_data[i].next_lo =
				linear_address((void far *)&pc98_data[i + 1]);
		else
			pc98_data[i].next_lo = 0;
		pc98_data[i].next_hi = 0;
	}
}

static void boot_kernel(void)
{
	u32 params_addr = linear_address((void far *)boot_params);
	u32 entry_addr = kernel_entry;
	u16 gdtr_addr = FP_OFF((void far *)gdtr);

	printf("BIOS drive %02Xh, logical CHS */%u/%u\n", bios_drive,
	       bios_heads, bios_sectors);
	printf("Starting Linux at %08lX...\n", entry_addr);
	enable_a20();
	outp(0x0a, 0xff);
	outp(0x5f, 0xff);
	outp(0x02, 0x7f);
	outp(0x5f, 0x7f);
	outp(0x50, 0x7f);
	__asm {
		cli
		mov bx, gdtr_addr
		db 0fh
		db 01h
		db 17h                 /* lgdt [bx] */
		mov esi, params_addr
		mov ecx, entry_addr
		mov eax, cr0
		or al, 1
		mov cr0, eax
		jmp boot_pm
	boot_pm:
		mov ax, 10h
		mov ds, ax
		mov es, ax
		mov fs, ax
		mov gs, ax
		mov ss, ax
		mov esp, 90000h
		xor ebp, ebp
		xor edi, edi
		xor ebx, ebx
		mov edx, 8
		push edx
		push ecx
		db 66h
		retf
	}
}

int main(int argc, char **argv)
{
	u8 far *bios_work_drive = (u8 far *)MK_FP(0, 0x584);
	u8 work_drive;
	unsigned drive;
	int i, arg_index = 2;

	if (argc < 2) {
		printf("Usage: LINUX98 VMLINUX [ide0-ide3|scsi0-scsi7] "
		       "[kernel arguments]\n");
		return 1;
	}
	printf("PC-98 Linux ELF loader for DOS\n");
	printf("Copyright (C) 2005, 2026, Awe Morris.\n\n");
	work_drive = *bios_work_drive;
	if ((work_drive & 0xf8) == 0xa0 ||
	    (work_drive & 0xfc) == 0x80)
		drive = work_drive;
	else
		drive = 0x80 | (work_drive & 3);
	if (argc >= 3 && !parse_bios_drive(argv[2], &drive)) {
		arg_index = 3;
	}
	bios_drive = drive;
	if (sense_disk(drive, &bios_heads, &bios_sectors)) {
		printf("INT 1Bh SENSE failed for BIOS drive %02Xh\n", drive);
		return 1;
	}
	probe_disks();
	command_line[0] = 0;
	for (i = arg_index; i < argc; i++) {
		if (strlen(command_line) + strlen(argv[i]) + 2 >= sizeof(command_line)) {
			printf("Kernel command line is too long\n");
			return 1;
		}
		if (command_line[0])
			strcat(command_line, " ");
		strcat(command_line, argv[i]);
	}
	*(u16 *)&gdtr[0] = sizeof(gdt) - 1;
	*(u32 *)&gdtr[2] = linear_address((void far *)gdt);
	/* Expose 15-16 MiB as RAM before any PT_LOAD copy reaches it. */
	outp(0x043b, 0x04);
	outp(0x005f, 0x04);
	if (load_kernel(argv[1]))
		return 1;
	build_boot_params();
	boot_kernel();
	return 0;
}
