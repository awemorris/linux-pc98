/*
 * Minimal BOOT98 installer for NEC PC-9800 real-mode DOS.
 *
 * Copyright (C) 2026 Awe Morris
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This program deliberately does only three things:
 *
 *   INST /LBA0       write IPL-LBA0.IMG to LBA 0 of the current drive's disk
 *   INST /LBA2       write IPL-LBA2.IMG to LBA 2 through 15
 *   INST /PART C:    install the FAT16 PBR and contiguous IO.SYS on C:
 *
 * A DOS drive letter is mapped back to an IDE disk and PC-98 partition by
 * comparing its DOS logical-sector-zero image with each partition's data
 * start sector.  Disk writes use PC-98 BIOS INT 1Bh absolute CHS requests.
 */

#include <ctype.h>
#include <dos.h>
#include <i86.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PHYSICAL_SECTOR_SIZE 512
#define DOS_SECTOR_BUFFER_SIZE 2048
#define PARTITION_TABLE_LBA 1
#define PARTITION_COUNT 16
#define PARTITION_ENTRY_SIZE 32
#define PARTITION_PBR_SECTORS 2
#define IO_SYS_MAX_SIZE (127UL * PHYSICAL_SECTOR_SIZE)

#define BIOS_SENSE 0x84
#define BIOS_WRITE 0x05
#define BIOS_READ 0x06

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned long u32;

struct target_partition {
	u8 bios_drive;
	u8 dos_drive;
	u8 index;
	u8 heads;
	u8 sectors;
	u16 cylinders;
	u32 ipl_lba;
	u32 data_lba;
	u32 end_lba;
	u8 entry[PARTITION_ENTRY_SIZE];
};

static u8 sector_buffer[PHYSICAL_SECTOR_SIZE];
static u8 verify_buffer[PHYSICAL_SECTOR_SIZE];
static u8 partition_table[PHYSICAL_SECTOR_SIZE];
static u8 dos_sector_buffer[DOS_SECTOR_BUFFER_SIZE];

static u8 io_drive;
static u8 io_command;
static u8 io_head;
static u8 io_sector;
static u8 io_status;
static u16 io_cylinder;
static u16 io_flags;

static u8 absolute_dos_drive;
static u16 absolute_dos_sector;
static u16 absolute_dos_status;
static u16 absolute_dos_flags;

static u16 get16(const u8 *value)
{
	return (u16)value[0] | ((u16)value[1] << 8);
}

static int same_word(const char *left, const char *right)
{
	while (*left && *right) {
		if (toupper((unsigned char)*left) !=
		    toupper((unsigned char)*right))
			return 0;
		left++;
		right++;
	}
	return !*left && !*right;
}

static int parse_dos_drive(const char *text, u8 *drive)
{
	if (strlen(text) != 2 || text[1] != ':' ||
	    !isalpha((unsigned char)text[0]))
		return -1;
	*drive = (u8)(toupper((unsigned char)text[0]) - 'A');
	return *drive < 26 ? 0 : -1;
}

static u8 current_dos_drive(void)
{
	unsigned drive;

	_dos_getdrive(&drive);
	return (u8)(drive - 1);
}

/* INT 25h leaves a flags word on the caller's stack. */
static void dos_absolute_read(void)
{
	__asm {
		push ds
		mov al, absolute_dos_drive
		mov cx, 1
		mov dx, absolute_dos_sector
		lea bx, dos_sector_buffer
		int 25h
		mov absolute_dos_status, ax
		pop ax
		mov absolute_dos_flags, ax
		pop ds
	}
}

static int read_dos_sector(u8 drive, u16 sector)
{
	absolute_dos_drive = drive;
	absolute_dos_sector = sector;
	absolute_dos_status = 0xffff;
	absolute_dos_flags = 1;
	memset(dos_sector_buffer, 0, sizeof(dos_sector_buffer));
	dos_absolute_read();
	if (absolute_dos_flags & 1) {
		printf("Cannot read DOS drive %c: sector %u (error %04Xh)\n",
		       'A' + drive, sector, absolute_dos_status);
		return -1;
	}
	return 0;
}

static int read_dos_boot_sector(u8 drive)
{
	return read_dos_sector(drive, 0);
}

static int sense_disk(u8 drive, u16 *cylinders, u8 *heads, u8 *sectors)
{
	union REGS input, output;

	memset(&input, 0, sizeof(input));
	input.h.ah = BIOS_SENSE;
	input.h.al = drive;
	input.x.bx = 0x0100;
	int86(0x1b, &input, &output);
	if (output.x.cflag || output.x.bx != PHYSICAL_SECTOR_SIZE ||
	    !output.x.cx || !output.h.dh || !output.h.dl)
		return -1;
	*cylinders = output.x.cx;
	*heads = output.h.dh;
	*sectors = output.h.dl;
	return 0;
}

/* Execute one absolute-CHS fixed-disk transfer using ES:BP. */
static void bios_io(void)
{
	__asm {
		push bp
		push es
		mov ax, ds
		mov es, ax
		lea bp, sector_buffer
		mov al, io_drive
		mov ah, io_command
		mov bx, PHYSICAL_SECTOR_SIZE
		mov cx, io_cylinder
		mov dh, io_head
		mov dl, io_sector
		int 1bh
		mov io_status, ah
		pushf
		pop ax
		mov io_flags, ax
		pop es
		pop bp
	}
}

static int transfer_sector(u8 command, u8 drive, u8 heads, u8 sectors,
			   u32 lba)
{
	u32 cylinder_size = (u32)heads * sectors;
	u32 remainder;

	io_cylinder = (u16)(lba / cylinder_size);
	remainder = lba % cylinder_size;
	io_head = (u8)(remainder / sectors);
	io_sector = (u8)(remainder % sectors);
	io_drive = drive;
	io_command = command;
	io_status = 0xff;
	io_flags = 1;
	bios_io();
	return (io_flags & 1) || io_status >= 0x20 ? -1 : 0;
}

static u32 chs_lba(const u8 *chs, u8 heads, u8 sectors)
{
	u16 cylinder = get16(chs + 2);

	return ((u32)cylinder * heads + chs[1]) * sectors + chs[0];
}

/* Find the physical IDE partition exported by DOS as a drive letter. */
static int find_partition(u8 dos_drive, struct target_partition *target)
{
	u8 bios_drive;
	int matches = 0;

	if (read_dos_boot_sector(dos_drive))
		return -1;
	for (bios_drive = 0x80; bios_drive <= 0x83; bios_drive++) {
		u16 cylinders;
		u8 heads, sectors;
		int index;

		if (sense_disk(bios_drive, &cylinders, &heads, &sectors))
			continue;
		if (transfer_sector(BIOS_READ, bios_drive, heads, sectors,
				    PARTITION_TABLE_LBA))
			continue;
		memcpy(partition_table, sector_buffer, sizeof(partition_table));
		for (index = 0; index < PARTITION_COUNT; index++) {
			u8 *entry = partition_table + index * PARTITION_ENTRY_SIZE;
			u32 data_lba;

			if (!entry[0])
				continue;
			data_lba = chs_lba(entry + 8, heads, sectors);
			if (transfer_sector(BIOS_READ, bios_drive, heads, sectors,
					    data_lba))
				continue;
			if (memcmp(sector_buffer, dos_sector_buffer,
				   PHYSICAL_SECTOR_SIZE))
				continue;
			matches++;
			memset(target, 0, sizeof(*target));
			target->bios_drive = bios_drive;
			target->dos_drive = dos_drive;
			target->index = (u8)index;
			target->heads = heads;
			target->sectors = sectors;
			target->cylinders = cylinders;
			target->ipl_lba = chs_lba(entry + 4, heads, sectors);
			target->data_lba = data_lba;
			target->end_lba = chs_lba(entry + 12, heads, sectors);
			memcpy(target->entry, entry, PARTITION_ENTRY_SIZE);
		}
	}
	if (matches != 1) {
		printf("DOS drive %c: maps to %d PC-98 IDE partitions; expected 1.\n",
		       'A' + dos_drive, matches);
		return -1;
	}
	return 0;
}

static void source_path(char *output, const char *program,
			const char *filename)
{
	const char *slash = strrchr(program, '\\');
	const char *forward = strrchr(program, '/');
	const char *colon = strrchr(program, ':');
	const char *end = slash;
	size_t prefix;

	if (forward && (!end || forward > end))
		end = forward;
	if (colon && (!end || colon > end))
		end = colon;
	prefix = end ? (size_t)(end - program + 1) : 0;
	if (prefix > 110)
		prefix = 0;
	memcpy(output, program, prefix);
	strcpy(output + prefix, filename);
}

static int write_file(const char *path, const struct target_partition *target,
		      u32 first_lba, unsigned sectors, int merge_bpb)
{
	FILE *input;
	long size;
	unsigned number;
	u8 bpb[0x3e];

	input = fopen(path, "rb");
	if (!input) {
		printf("Cannot open %s\n", path);
		return -1;
	}
	fseek(input, 0, SEEK_END);
	size = ftell(input);
	rewind(input);
	if (size <= 0 || size > (long)sectors * PHYSICAL_SECTOR_SIZE ||
	    (!merge_bpb && size != (long)sectors * PHYSICAL_SECTOR_SIZE)) {
		printf("Unexpected size for %s: %ld bytes\n", path, size);
		fclose(input);
		return -1;
	}
	if (merge_bpb)
		memcpy(bpb, dos_sector_buffer, sizeof(bpb));
	for (number = 0; number < sectors; number++) {
		size_t bytes;

		memset(sector_buffer, 0, sizeof(sector_buffer));
		bytes = fread(sector_buffer, 1, sizeof(sector_buffer), input);
		if (ferror(input)) {
			fclose(input);
			return -1;
		}
		if (!number && merge_bpb) {
			memcpy(sector_buffer + 3, bpb + 3, 0x3e - 3);
			sector_buffer[0x1fe] = 0x55;
			sector_buffer[0x1ff] = 0xaa;
		}
		if (transfer_sector(BIOS_WRITE, target->bios_drive,
				    target->heads, target->sectors,
				    first_lba + number)) {
			printf("Write failed at LBA %lu (status %02Xh).\n",
			       first_lba + number, io_status);
			fclose(input);
			return -1;
		}
		memcpy(verify_buffer, sector_buffer, sizeof(verify_buffer));
		if (transfer_sector(BIOS_READ, target->bios_drive,
				    target->heads, target->sectors,
				    first_lba + number) ||
		    memcmp(sector_buffer, verify_buffer, sizeof(verify_buffer))) {
			printf("Verify failed at LBA %lu.\n", first_lba + number);
			fclose(input);
			return -1;
		}
		if (!bytes && ftell(input) < size) {
			fclose(input);
			return -1;
		}
	}
	fclose(input);
	printf("Wrote %u sectors from %s at LBA %lu.\n",
	       sectors, path, first_lba);
	return 0;
}

static int install_disk_image(const char *program, const char *filename,
			      u32 lba, unsigned sectors)
{
	struct target_partition target;
	char path[128];
	u8 drive = current_dos_drive();

	if (find_partition(drive, &target))
		return 1;
	source_path(path, program, filename);
	printf("Current drive %c: is IDE%u partition %u.\n",
	       'A' + drive, target.bios_drive - 0x80, target.index + 1);
	return write_file(path, &target, lba, sectors, 0) ? 1 : 0;
}

static int copy_io_sys(const char *program, u8 drive, u32 *copied_size)
{
	char source_pathname[128];
	char target_pathname[16];
	FILE *source;
	FILE *target;
	size_t count;
	u32 total = 0;

	source_path(source_pathname, program, "IO.SYS");
	sprintf(target_pathname, "%c:\\IO.SYS", 'A' + drive);
	source = fopen(source_pathname, "rb");
	if (!source) {
		printf("Cannot open %s\n", source_pathname);
		return -1;
	}
	_dos_setfileattr(target_pathname, _A_NORMAL);
	remove(target_pathname);
	target = fopen(target_pathname, "wb");
	if (!target) {
		printf("Cannot create %s\n", target_pathname);
		fclose(source);
		return -1;
	}
	while ((count = fread(dos_sector_buffer, 1,
			       sizeof(dos_sector_buffer), source)) != 0) {
		if (fwrite(dos_sector_buffer, 1, count, target) != count) {
			printf("Write failed for %s\n", target_pathname);
			fclose(target);
			fclose(source);
			return -1;
		}
		total += count;
	}
	if (ferror(source) || fclose(target)) {
		printf("Copy failed for %s\n", target_pathname);
		fclose(source);
		return -1;
	}
	fclose(source);
	if (!total || total > IO_SYS_MAX_SIZE) {
		printf("Invalid IO.SYS size: %lu bytes\n", total);
		return -1;
	}
	_dos_setfileattr(target_pathname, _A_RDONLY | _A_HIDDEN | _A_SYSTEM);
	*copied_size = total;
	printf("Copied %s (%lu bytes).\n", target_pathname, total);
	return 0;
}

/*
 * The compact PBR reads IO.SYS as a linear extent.  This is the traditional
 * DOS SYS-file rule, not an undocumented reserved-sector copy: IO.SYS remains
 * a normal FAT16 file and every byte belongs to its cluster chain.
 */
static int verify_contiguous_io_sys(u8 drive, u32 expected_size)
{
	u16 bytes_per_sector;
	u16 reserved;
	u16 root_entries;
	u16 sectors_per_fat;
	u8 sectors_per_cluster;
	u8 fat_count;
	u16 root_start;
	u16 root_sectors;
	u16 first_cluster = 0;
	u32 file_size = 0;
	u32 cluster_bytes;
	u32 clusters_needed;
	u32 entry_number;
	u16 cluster;
	u32 index;

	if (read_dos_boot_sector(drive))
		return -1;
	bytes_per_sector = get16(dos_sector_buffer + 0x0b);
	reserved = get16(dos_sector_buffer + 0x0e);
	root_entries = get16(dos_sector_buffer + 0x11);
	sectors_per_fat = get16(dos_sector_buffer + 0x16);
	sectors_per_cluster = dos_sector_buffer[0x0d];
	fat_count = dos_sector_buffer[0x10];
	if (bytes_per_sector != 1024 || reserved != 1 ||
	    !sectors_per_cluster || !fat_count ||
	    !root_entries || !sectors_per_fat) {
		printf("%c: must be FAT16 with one 1024-byte reserved sector.\n",
		       'A' + drive);
		return -1;
	}
	root_start = reserved + fat_count * sectors_per_fat;
	root_sectors = (u16)(((u32)root_entries * 32 +
			      bytes_per_sector - 1) / bytes_per_sector);
	for (entry_number = 0; entry_number < root_entries; entry_number++) {
		u16 sector = root_start +
			(u16)((entry_number * 32) / bytes_per_sector);
		u16 offset = (u16)((entry_number * 32) % bytes_per_sector);

		if (!offset && read_dos_sector(drive, sector))
			return -1;
		if (!memcmp(dos_sector_buffer + offset, "IO      SYS", 11)) {
			first_cluster = get16(dos_sector_buffer + offset + 0x1a);
			file_size = (u32)get16(dos_sector_buffer + offset + 0x1c) |
				    ((u32)get16(dos_sector_buffer + offset + 0x1e) << 16);
			break;
		}
	}
	(void)root_sectors;
	if (first_cluster < 2 || file_size != expected_size) {
		printf("Cannot locate the newly copied IO.SYS in the FAT root.\n");
		return -1;
	}
	cluster_bytes = (u32)bytes_per_sector * sectors_per_cluster;
	clusters_needed = (file_size + cluster_bytes - 1) / cluster_bytes;
	cluster = first_cluster;
	for (index = 1; index < clusters_needed; index++) {
		u32 fat_offset = (u32)cluster * 2;
		u16 fat_sector = reserved + (u16)(fat_offset / bytes_per_sector);
		u16 fat_index = (u16)(fat_offset % bytes_per_sector);
		u16 next;

		if (read_dos_sector(drive, fat_sector))
			return -1;
		next = get16(dos_sector_buffer + fat_index);
		if (next != (u16)(cluster + 1)) {
			printf("IO.SYS is fragmented at cluster %u -> %u.\n",
			       cluster, next);
			printf("Reformat the BOOT volume and run INST /PART again.\n");
			return -1;
		}
		cluster = next;
	}
	{
		u32 fat_offset = (u32)cluster * 2;
		u16 fat_sector = reserved + (u16)(fat_offset / bytes_per_sector);
		u16 fat_index = (u16)(fat_offset % bytes_per_sector);

		if (read_dos_sector(drive, fat_sector))
			return -1;
		if (get16(dos_sector_buffer + fat_index) < 0xfff8) {
			printf("IO.SYS has an invalid FAT16 end marker.\n");
			return -1;
		}
	}
	printf("IO.SYS is contiguous from cluster %u.\n", first_cluster);
	return 0;
}

static int install_partition(const char *program, const char *drive_text)
{
	struct target_partition target;
	char path[128];
	u8 drive;
	u32 io_size;

	if (parse_dos_drive(drive_text, &drive) || find_partition(drive, &target))
		return 1;
	if (memcmp(target.entry + 16, "BOOT            ", 16)) {
		printf("Partition %c: is not named BOOT.\n", 'A' + drive);
		return 1;
	}
	if (target.ipl_lba != target.data_lba) {
		printf("Partition %c: must use the same IPL and data start CHS.\n",
		       'A' + drive);
		return 1;
	}
	if (copy_io_sys(program, drive, &io_size) ||
	    verify_contiguous_io_sys(drive, io_size))
		return 1;
	/* verify_contiguous_io_sys leaves a FAT sector in the shared buffer. */
	if (read_dos_boot_sector(drive))
		return 1;
	source_path(path, program, "IPL-PART.IMG");
	printf("Drive %c: is IDE%u partition %u; PBR LBA %lu.\n",
	       'A' + drive, target.bios_drive - 0x80,
	       target.index + 1, target.ipl_lba);
	return write_file(path, &target, target.ipl_lba,
			  PARTITION_PBR_SECTORS, 1) ? 1 : 0;
}

static void usage(void)
{
	printf("Usage:\n");
	printf("  INST /LBA0       write IPL-LBA0.IMG to current drive's disk\n");
	printf("  INST /LBA2       write IPL-LBA2.IMG to LBA 2-15\n");
	printf("  INST /PART C:    install FAT16 PBR and IO.SYS on C:\n");
}

int main(int argc, char **argv)
{
	printf("BOOT98 installer for PC-9800 DOS\n");
	printf("Copyright (C) 2026 Awe Morris\n\n");
	if (argc == 2 && same_word(argv[1], "/LBA0"))
		return install_disk_image(argv[0], "IPL-LBA0.IMG", 0, 1);
	if (argc == 2 && same_word(argv[1], "/LBA2"))
		return install_disk_image(argv[0], "IPL-LBA2.IMG", 2, 14);
	if (argc == 3 && same_word(argv[1], "/PART"))
		return install_partition(argv[0], argv[2]);
	usage();
	return 1;
}
