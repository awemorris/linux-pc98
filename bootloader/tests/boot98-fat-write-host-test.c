/* Destructive host-side regression tests for the BOOT98 FAT16 writer. */

#include "boot98-fat16.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_BASE_LBA 7U
#define TEST_TOTAL_LOGICAL 5000U
#define TEST_FAT_LOGICAL 20U
#define TEST_ROOT_ENTRIES 32U

struct memory_disk {
	uint8_t *bytes;
	uint32_t sectors;
	uint32_t writes;
	uint32_t fail_write_at;
	uint32_t scale;
	uint32_t fat_start;
	uint32_t fat_sectors;
	uint32_t root_start;
	uint32_t root_sectors;
	uint32_t data_start;
	uint32_t cluster_count;
};

static void put16(uint8_t *bytes, uint16_t value)
{
	bytes[0] = (uint8_t)value;
	bytes[1] = (uint8_t)(value >> 8);
}

static void put32(uint8_t *bytes, uint32_t value)
{
	put16(bytes, (uint16_t)value);
	put16(bytes + 2, (uint16_t)(value >> 16));
}

static uint16_t get16(const uint8_t *bytes)
{
	return bytes[0] | ((uint16_t)bytes[1] << 8);
}

static uint8_t *sector(struct memory_disk *disk, uint32_t relative_lba)
{
	assert(TEST_BASE_LBA + relative_lba < disk->sectors);
	return disk->bytes + (TEST_BASE_LBA + relative_lba) * 512U;
}

static int memory_read(const void *context, uint32_t lba, void *buffer)
{
	const struct memory_disk *disk = context;

	if (lba >= disk->sectors)
		return 0;
	memcpy(buffer, disk->bytes + lba * 512U, 512);
	return 1;
}

static int memory_write(void *context, uint32_t lba, const void *buffer)
{
	struct memory_disk *disk = context;

	if (lba >= disk->sectors || disk->writes == disk->fail_write_at)
		return 0;
	memcpy(disk->bytes + lba * 512U, buffer, 512);
	disk->writes++;
	return 1;
}

static void set_fat(struct memory_disk *disk, unsigned copy,
		    uint32_t cluster, uint16_t value)
{
	uint32_t offset = cluster * 2U;
	uint32_t lba = disk->fat_start + copy * disk->fat_sectors +
		       offset / 512U;

	put16(sector(disk, lba) + offset % 512U, value);
}

static uint16_t get_fat(struct memory_disk *disk, unsigned copy,
			uint32_t cluster)
{
	uint32_t offset = cluster * 2U;
	uint32_t lba = disk->fat_start + copy * disk->fat_sectors +
		       offset / 512U;

	return get16(sector(disk, lba) + offset % 512U);
}

static void assert_fats_equal(struct memory_disk *disk)
{
	assert(!memcmp(sector(disk, disk->fat_start),
		       sector(disk, disk->fat_start + disk->fat_sectors),
		       disk->fat_sectors * 512U));
}

static void format_disk(struct memory_disk *disk, uint16_t logical_bytes)
{
	uint32_t physical_total;
	uint8_t *bpb;

	memset(disk, 0, sizeof(*disk));
	disk->scale = logical_bytes / 512U;
	physical_total = TEST_TOTAL_LOGICAL * disk->scale;
	disk->sectors = TEST_BASE_LBA + physical_total;
	disk->bytes = calloc(disk->sectors, 512U);
	assert(disk->bytes);
	disk->fail_write_at = UINT32_MAX;
	disk->fat_start = disk->scale;
	disk->fat_sectors = TEST_FAT_LOGICAL * disk->scale;
	disk->root_start = disk->fat_start + 2U * disk->fat_sectors;
	disk->root_sectors = (TEST_ROOT_ENTRIES * 32U + 511U) / 512U;
	disk->data_start = disk->root_start + disk->root_sectors;
	disk->cluster_count =
		(physical_total - disk->data_start) / disk->scale;
	assert(disk->cluster_count >= 4085U);

	bpb = sector(disk, 0);
	put16(bpb + 11, logical_bytes);
	bpb[13] = 1;
	put16(bpb + 14, 1);
	bpb[16] = 2;
	put16(bpb + 17, TEST_ROOT_ENTRIES);
	put16(bpb + 19, TEST_TOTAL_LOGICAL);
	put16(bpb + 22, TEST_FAT_LOGICAL);
	set_fat(disk, 0, 0, 0xfff8);
	set_fat(disk, 0, 1, 0xffff);
	set_fat(disk, 1, 0, 0xfff8);
	set_fat(disk, 1, 1, 0xffff);
}

static void destroy_disk(struct memory_disk *disk)
{
	free(disk->bytes);
	disk->bytes = 0;
}

static void mount_disk(struct memory_disk *disk,
		       struct boot98_filesystem *filesystem)
{
	const struct boot98_filesystem_driver *const drivers[] = {
		&boot98_fat16_driver,
	};
	struct boot98_volume volume = {
		.context = disk,
		.start_lba = TEST_BASE_LBA,
		.sector_size = 512,
		.read = memory_read,
		.write = memory_write,
	};

	assert(boot98_fs_mount_result(filesystem, &volume, drivers, 1) ==
	       BOOT98_FS_OK);
}

static void reopen_and_read(struct boot98_filesystem *filesystem,
			    const char *path, void *buffer, uint32_t size)
{
	struct boot98_file file;

	assert(boot98_fs_open_result(filesystem, path, &file) == BOOT98_FS_OK);
	assert(file.size == size);
	if (size)
		assert(boot98_file_read_result(&file, 0, buffer, size, 0, 0) ==
		       BOOT98_FS_OK);
}

static void test_create_write_truncate(uint16_t logical_bytes)
{
	struct memory_disk disk;
	struct boot98_filesystem filesystem;
	struct boot98_file file;
	struct boot98_dirent entry;
	uint8_t buffer[1031];
	uint32_t cluster;

	format_disk(&disk, logical_bytes);
	mount_disk(&disk, &filesystem);
	assert(boot98_fs_create_result(&filesystem, "/marker.txt", &file) ==
	       BOOT98_FS_OK);
	assert(file.size == 0);
	assert(boot98_file_write_result(&file, 0, "hello", 5) == BOOT98_FS_OK);
	assert(boot98_file_flush_result(&file) == BOOT98_FS_OK);
	memset(buffer, 0, sizeof(buffer));
	reopen_and_read(&filesystem, "/MARKER.TXT", buffer, 5);
	assert(!memcmp(buffer, "hello", 5));
	assert_fats_equal(&disk);

	assert(boot98_file_write_result(&file, 1, "XYZ", 3) == BOOT98_FS_OK);
	assert(boot98_file_write_result(&file, 1027, "tail", 4) ==
	       BOOT98_FS_OK);
	assert(boot98_file_flush_result(&file) == BOOT98_FS_OK);
	memset(buffer, 0xa5, sizeof(buffer));
	reopen_and_read(&filesystem, "/marker.txt", buffer, sizeof(buffer));
	assert(!memcmp(buffer, "hXYZo", 5));
	for (unsigned i = 5; i < 1027; i++)
		assert(buffer[i] == 0);
	assert(!memcmp(buffer + 1027, "tail", 4));
	assert_fats_equal(&disk);

	assert(boot98_file_truncate_result(&file, 3) == BOOT98_FS_OK);
	assert(boot98_file_flush_result(&file) == BOOT98_FS_OK);
	memset(buffer, 0, sizeof(buffer));
	reopen_and_read(&filesystem, "/marker.txt", buffer, 3);
	assert(!memcmp(buffer, "hXY", 3));
	assert(boot98_fs_stat_result(&filesystem, "/marker.txt", &entry) ==
	       BOOT98_FS_OK);
	assert(entry.size == 3);

	assert(boot98_file_truncate_result(&file, 0) == BOOT98_FS_OK);
	assert(boot98_file_flush_result(&file) == BOOT98_FS_OK);
	reopen_and_read(&filesystem, "/marker.txt", buffer, 0);
	for (cluster = 2; cluster < 8; cluster++) {
		assert(get_fat(&disk, 0, cluster) == 0);
		assert(get_fat(&disk, 1, cluster) == 0);
	}
	assert_fats_equal(&disk);
	destroy_disk(&disk);
}

static void test_existing_allocated_empty_file(void)
{
	struct memory_disk disk;
	struct boot98_filesystem filesystem;
	struct boot98_file file;
	uint8_t *raw;

	format_disk(&disk, 512);
	raw = sector(&disk, disk.root_start);
	memcpy(raw, "EMPTY   BIN", 11);
	raw[11] = 0x20;
	put16(raw + 26, 2);
	set_fat(&disk, 0, 2, 0xffff);
	set_fat(&disk, 1, 2, 0xffff);
	mount_disk(&disk, &filesystem);
	assert(boot98_fs_create_result(&filesystem, "/empty.bin", &file) ==
	       BOOT98_FS_OK);
	assert(file.size == 0 && get_fat(&disk, 0, 2) == 0 &&
	       get_fat(&disk, 1, 2) == 0);
	destroy_disk(&disk);
}

static void test_create_in_existing_directory(uint16_t logical_bytes)
{
	struct memory_disk disk;
	struct boot98_filesystem filesystem;
	struct boot98_file file;
	uint8_t buffer[6] = { 0 };
	uint8_t *root;

	format_disk(&disk, logical_bytes);
	root = sector(&disk, disk.root_start);
	memcpy(root, "CMD        ", 11);
	root[11] = 0x10;
	put16(root + 26, 2);
	set_fat(&disk, 0, 2, 0xffff);
	set_fat(&disk, 1, 2, 0xffff);
	mount_disk(&disk, &filesystem);
	assert(boot98_fs_create_result(&filesystem, "CMD/EDIT.TXT", &file) ==
	       BOOT98_FS_OK);
	assert(boot98_file_write_result(&file, 0, "Boots", 5) == BOOT98_FS_OK);
	assert(boot98_file_flush_result(&file) == BOOT98_FS_OK);
	reopen_and_read(&filesystem, "/cmd/edit.txt", buffer, 5);
	assert(!memcmp(buffer, "Boots", 5));
	assert_fats_equal(&disk);
	destroy_disk(&disk);
}

static void test_full_root(void)
{
	struct memory_disk disk;
	struct boot98_filesystem filesystem;
	struct boot98_file file;
	uint8_t *root;

	format_disk(&disk, 512);
	root = sector(&disk, disk.root_start);
	for (unsigned i = 0; i < TEST_ROOT_ENTRIES; i++) {
		uint8_t *raw = root + i * 32U;

		memset(raw, ' ', 11);
		raw[0] = (uint8_t)('A' + i % 26);
		raw[1] = (uint8_t)('0' + i % 10);
		raw[11] = 0x20;
	}
	mount_disk(&disk, &filesystem);
	assert(boot98_fs_create_result(&filesystem, "/full.bin", &file) ==
	       BOOT98_FS_NO_SPACE);
	destroy_disk(&disk);
}

static void test_full_disk(void)
{
	struct memory_disk disk;
	struct boot98_filesystem filesystem;
	struct boot98_file file;

	format_disk(&disk, 512);
	for (uint32_t cluster = 2; cluster < disk.cluster_count + 2; cluster++) {
		set_fat(&disk, 0, cluster, 0xffff);
		set_fat(&disk, 1, cluster, 0xffff);
	}
	mount_disk(&disk, &filesystem);
	assert(boot98_fs_create_result(&filesystem, "/full.bin", &file) ==
	       BOOT98_FS_OK);
	assert(boot98_file_write_result(&file, 0, "x", 1) ==
	       BOOT98_FS_NO_SPACE);
	destroy_disk(&disk);
}

static void test_corrupt_chain(void)
{
	struct memory_disk disk;
	struct boot98_filesystem filesystem;
	struct boot98_file file;
	uint8_t *raw;

	format_disk(&disk, 512);
	raw = sector(&disk, disk.root_start);
	memcpy(raw, "LOOP    BIN", 11);
	raw[11] = 0x20;
	put16(raw + 26, 2);
	put32(raw + 28, 1024);
	set_fat(&disk, 0, 2, 3);
	set_fat(&disk, 0, 3, 2);
	set_fat(&disk, 1, 2, 3);
	set_fat(&disk, 1, 3, 2);
	mount_disk(&disk, &filesystem);
	assert(boot98_fs_open_result(&filesystem, "/loop.bin", &file) ==
	       BOOT98_FS_OK);
	assert(boot98_file_truncate_result(&file, 0) == BOOT98_FS_CORRUPT);
	assert(get_fat(&disk, 0, 2) == 3 && get_fat(&disk, 0, 3) == 2);
	assert_fats_equal(&disk);
	destroy_disk(&disk);
}

static void test_write_error_not_exposed(void)
{
	struct memory_disk disk;
	struct boot98_filesystem filesystem;
	struct boot98_file file;
	uint8_t *raw;

	format_disk(&disk, 512);
	mount_disk(&disk, &filesystem);
	assert(boot98_fs_create_result(&filesystem, "/error.bin", &file) ==
	       BOOT98_FS_OK);
	disk.fail_write_at = disk.writes;
	assert(boot98_file_write_result(&file, 0, "x", 1) ==
	       BOOT98_FS_IO_ERROR);
	raw = sector(&disk, disk.root_start);
	assert(get16(raw + 26) == 0 &&
	       raw[28] == 0 && raw[29] == 0 && raw[30] == 0 && raw[31] == 0);
	destroy_disk(&disk);
}

int main(void)
{
	test_create_write_truncate(512);
	test_create_write_truncate(1024);
	test_create_in_existing_directory(512);
	test_create_in_existing_directory(1024);
	test_existing_allocated_empty_file();
	test_full_root();
	test_full_disk();
	test_corrupt_chain();
	test_write_error_not_exposed();
	puts("BOOT98 FAT16 write host tests: OK");
	return 0;
}
