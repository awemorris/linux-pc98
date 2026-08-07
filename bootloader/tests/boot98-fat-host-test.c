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
	unsigned write_count;
	int fail_reads;
	int fail_writes;
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

	if (disk->fail_reads || absolute_lba < TEST_BASE_LBA)
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

static int test_write(void *context, uint32_t absolute_lba,
		      const void *buffer)
{
	struct test_disk *disk = context;
	uint32_t lba;
	uint8_t *destination;

	if (disk->fail_writes || absolute_lba < TEST_BASE_LBA)
		return 0;
	lba = absolute_lba - TEST_BASE_LBA;
	if (!lba)
		destination = disk->bpb;
	else if (lba == disk->fat_lba)
		destination = disk->fat;
	else if (lba == disk->root_lba)
		destination = disk->root;
	else if (lba == disk->data_lba)
		destination = disk->data;
	else
		return 0;
	memcpy(destination, buffer, 512);
	disk->write_count++;
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
	/* Keep this regression focused on the legacy read-only contract.  The
	 * writable FAT16 path has its own destructive host-side test image. */
	volume.write = 0;
	assert(boot98_fs_mount(&filesystem, &volume, drivers, 1));
	assert(!strcmp(filesystem.driver->name, "fat16"));
	assert(boot98_fs_open_result(&filesystem, "/missing.bin", &file) ==
	       BOOT98_FS_NOT_FOUND);
	assert(boot98_fs_open_result(&filesystem, "/bad/path", &file) ==
	       BOOT98_FS_INVALID_PATH);
	assert(boot98_fs_create_result(&filesystem, "/new.bin", &file) ==
	       BOOT98_FS_READ_ONLY);
	assert(boot98_fs_open(&filesystem, "/kernel.bin", &file));
	assert(file.size == 5);
	assert(boot98_file_read(&file, 0, buffer, 5));
	assert(!strcmp(buffer, "hello"));
	assert(boot98_fs_readdir(&filesystem, "/", 0, &entry));
	assert(!strcmp(entry.name, "KERNEL.BIN"));
	assert(entry.size == 5 && entry.attributes == 0x20);
	assert(!boot98_fs_readdir(&filesystem, "/subdir", 0, &entry));
	assert(boot98_fs_readdir_result(&filesystem, "/subdir", 0, &entry) ==
	       BOOT98_FS_INVALID_PATH);
	assert(boot98_file_write_result(&file, 0, "x", 1) ==
	       BOOT98_FS_READ_ONLY);
	assert(boot98_file_truncate_result(&file, 0) == BOOT98_FS_READ_ONLY);
	assert(boot98_file_flush_result(&file) == BOOT98_FS_READ_ONLY);
	assert(boot98_fs_stat_result(&filesystem, "/kernel.bin", &entry) ==
	       BOOT98_FS_OK);
	assert(!strcmp(entry.name, "KERNEL.BIN") && entry.size == 5);
	assert(boot98_file_contiguous_lba(&file, &lba));
	assert(lba == TEST_BASE_LBA + disk.data_lba);
}

static void test_volume_write_contract(void)
{
	struct test_disk disk;
	struct boot98_volume volume;
	uint8_t original[512], replacement[512], observed[512];

	make_disk(&disk, 512);
	memcpy(original, disk.data, sizeof(original));
	memset(replacement, 0xa5, sizeof(replacement));
	volume.context = &disk;
	volume.start_lba = TEST_BASE_LBA;
	volume.sector_size = 512;
	volume.read = test_read;
	volume.write = test_write;
	assert(boot98_volume_write_result(&volume, disk.data_lba,
					  replacement) == BOOT98_FS_OK);
	assert(disk.write_count == 1);
	assert(boot98_volume_read_result(&volume, disk.data_lba, observed) ==
	       BOOT98_FS_OK);
	assert(!memcmp(observed, replacement, sizeof(observed)));
	assert(boot98_volume_write(&volume, disk.data_lba, original));
	assert(!memcmp(disk.data, original, sizeof(original)));

	volume.write = 0;
	assert(boot98_volume_write_result(&volume, disk.data_lba,
					  replacement) == BOOT98_FS_READ_ONLY);
	volume.write = test_write;
	disk.fail_writes = 1;
	assert(boot98_volume_write_result(&volume, disk.data_lba,
					  replacement) == BOOT98_FS_IO_ERROR);
	disk.fail_writes = 0;
	volume.start_lba = UINT32_MAX;
	assert(boot98_volume_write_result(&volume, 1, replacement) ==
	       BOOT98_FS_INVALID_ARGUMENT);
	volume.start_lba = TEST_BASE_LBA;
	volume.sector_size = 1024;
	assert(boot98_volume_write_result(&volume, disk.data_lba,
					  replacement) ==
	       BOOT98_FS_INVALID_ARGUMENT);
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
	volume.write = 0;
	assert(!boot98_fs_mount(&filesystem, &volume, drivers, 1));
}

static void test_probe_io_error(void)
{
	const struct boot98_filesystem_driver *const drivers[] = {
		&boot98_fat16_driver,
	};
	struct test_disk disk;
	struct boot98_volume volume;
	struct boot98_filesystem filesystem;

	make_disk(&disk, 512);
	disk.fail_reads = 1;
	volume.context = &disk;
	volume.start_lba = TEST_BASE_LBA;
	volume.sector_size = 512;
	volume.read = test_read;
	volume.write = 0;
	assert(boot98_fs_mount_result(&filesystem, &volume, drivers, 1) ==
	       BOOT98_FS_IO_ERROR);
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
	volume.write = 0;
	assert(!boot98_fs_mount(&filesystem, &volume, drivers, 1));
}

int main(void)
{
	test_fat16(512);
	test_fat16(1024);
	test_fat12_rejected();
	test_fat32_bpb_layout_rejected_for_fat16();
	test_probe_io_error();
	test_volume_write_contract();
	puts("BOOT98 FAT16 host tests: OK");
	return 0;
}
