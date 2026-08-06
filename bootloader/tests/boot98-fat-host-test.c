/* Host-side regression tests for the BOOT98 read-only FAT16 driver. */

#include "boot98-fat16.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define TEST_BASE_LBA 100U

struct test_disk {
	uint8_t bpb[512];
	uint8_t fat[512];
	uint8_t root[512];
	uint8_t data[512];
	uint32_t fat_lba;
	uint32_t root_lba;
	uint32_t data_lba;
};

static void put16(uint8_t *bytes, uint16_t value)
{
	bytes[0] = value;
	bytes[1] = value >> 8;
}

static void put32(uint8_t *bytes, uint32_t value)
{
	put16(bytes, value);
	put16(bytes + 2, value >> 16);
}

static int test_read(const void *context, uint32_t absolute_lba, void *buffer)
{
	const struct test_disk *disk = context;
	uint32_t lba;

	if (absolute_lba < TEST_BASE_LBA)
		return 0;
	lba = absolute_lba - TEST_BASE_LBA;
	memset(buffer, 0, 512);
	if (!lba)
		memcpy(buffer, disk->bpb, 512);
	else if (lba == disk->fat_lba)
		memcpy(buffer, disk->fat, 512);
	else if (lba == disk->root_lba)
		memcpy(buffer, disk->root, 512);
	else if (lba == disk->data_lba)
		memcpy(buffer, disk->data, 512);
	return 1;
}

static void make_disk(struct test_disk *disk, uint16_t logical_sector_size)
{
	uint8_t scale = logical_sector_size / 512;
	uint16_t fat_logical_sectors = 17;

	memset(disk, 0, sizeof(*disk));
	put16(disk->bpb + 11, logical_sector_size);
	disk->bpb[13] = 1;
	put16(disk->bpb + 14, 1);
	disk->bpb[16] = 1;
	put16(disk->bpb + 17, 16);
	put16(disk->bpb + 19, 4104);
	put16(disk->bpb + 22, fat_logical_sectors);
	disk->fat_lba = scale;
	disk->root_lba = scale + fat_logical_sectors * scale;
	disk->data_lba = disk->root_lba + 1;
	put16(disk->fat + 4, 0xffff);
	memcpy(disk->root, "KERNEL  BIN", 11);
	disk->root[11] = 0x20;
	put16(disk->root + 26, 2);
	put32(disk->root + 28, 5);
	memcpy(disk->data, "hello", 5);
}

static void test_fat16(uint16_t logical_sector_size)
{
	const struct boot98_filesystem_driver *const drivers[] = {
		&boot98_fat16_driver,
	};
	struct test_disk disk;
	struct boot98_volume volume;
	struct boot98_filesystem filesystem;
	struct boot98_file file;
	struct boot98_dirent entry;
	uint32_t lba = 0;
	char buffer[6] = { 0 };

	make_disk(&disk, logical_sector_size);
	volume.context = &disk;
	volume.start_lba = TEST_BASE_LBA;
	volume.sector_size = 512;
	volume.read = test_read;
	assert(boot98_fs_mount(&filesystem, &volume, drivers, 1));
	assert(!strcmp(filesystem.driver->name, "fat16"));
	assert(boot98_fs_open(&filesystem, "/kernel.bin", &file));
	assert(file.size == 5);
	assert(boot98_file_read(&file, 0, buffer, 5));
	assert(!strcmp(buffer, "hello"));
	assert(boot98_fs_readdir(&filesystem, "/", 0, &entry));
	assert(!strcmp(entry.name, "KERNEL.BIN"));
	assert(entry.size == 5 && entry.attributes == 0x20);
	assert(!boot98_fs_readdir(&filesystem, "/subdir", 0, &entry));
	assert(boot98_file_contiguous_lba(&file, &lba));
	assert(lba == TEST_BASE_LBA + disk.data_lba);
}

static void test_fat12_rejected(void)
{
	const struct boot98_filesystem_driver *const drivers[] = {
		&boot98_fat16_driver,
	};
	struct test_disk disk;
	struct boot98_volume volume;
	struct boot98_filesystem filesystem;

	make_disk(&disk, 512);
	put16(disk.bpb + 19, 1024);
	volume.context = &disk;
	volume.start_lba = TEST_BASE_LBA;
	volume.sector_size = 512;
	volume.read = test_read;
	assert(!boot98_fs_mount(&filesystem, &volume, drivers, 1));
}

static void test_fat32_bpb_layout_rejected_for_fat16(void)
{
	const struct boot98_filesystem_driver *const drivers[] = {
		&boot98_fat16_driver,
	};
	struct test_disk disk;
	struct boot98_volume volume;
	struct boot98_filesystem filesystem;

	make_disk(&disk, 512);
	put16(disk.bpb + 22, 0);
	put32(disk.bpb + 36, 17);
	volume.context = &disk;
	volume.start_lba = TEST_BASE_LBA;
	volume.sector_size = 512;
	volume.read = test_read;
	assert(!boot98_fs_mount(&filesystem, &volume, drivers, 1));
}

int main(void)
{
	test_fat16(512);
	test_fat16(1024);
	test_fat12_rejected();
	test_fat32_bpb_layout_rejected_for_fat16();
	puts("BOOT98 FAT16 host tests: OK");
	return 0;
}
