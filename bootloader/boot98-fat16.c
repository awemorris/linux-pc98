/*
 * PC-9800 Bootloader FAT16 driver
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "boot98-fat.h"
#include "boot98-fat16.h"

#define FAT16_END_OF_CHAIN 0xfff0U

static enum boot98_fs_result fat16_probe(const struct boot98_volume *volume)
{
	return boot98_fat_probe(volume, BOOT98_FAT16);
}

static enum boot98_fs_result fat16_mount(
	struct boot98_filesystem *filesystem)
{
	struct boot98_fat_state *fat;
	enum boot98_fs_result result;

	result = boot98_fat_mount(filesystem, BOOT98_FAT16);
	if (result != BOOT98_FS_OK)
		return result;
	fat = boot98_fat_state(filesystem);
	return fat->root_entries && fat->fat_sectors ?
		BOOT98_FS_OK : BOOT98_FS_CORRUPT;
}

static enum boot98_fs_result fat16_next_cluster(
	struct boot98_filesystem *filesystem, uint32_t cluster,
	uint32_t *next_cluster)
{
	struct boot98_fat_state *fat = boot98_fat_state(filesystem);
	uint32_t offset = cluster * 2;
	const uint8_t *sector = boot98_fat_read_sector(
		filesystem, fat->fat_start + (offset >> 9));

	if (!sector)
		return BOOT98_FS_IO_ERROR;
	*next_cluster = boot98_fat_get16(sector + (offset & 511));
	return BOOT98_FS_OK;
}

static enum boot98_fs_result fat16_open(
	struct boot98_filesystem *filesystem, const char *path,
	struct boot98_file *file)
{
	struct boot98_fat_state *fat = boot98_fat_state(filesystem);
	struct boot98_fat_file_state *fat_file = boot98_fat_file_state(file);
	const uint8_t *sector = 0;
	char canonical[11];

	if (!boot98_fat_short_name(path, canonical))
		return BOOT98_FS_INVALID_PATH;
	for (uint32_t index = 0; index < fat->root_entries; index++) {
		const uint8_t *raw;

		if (!(index & 15))
			sector = boot98_fat_read_sector(
				filesystem, fat->root_start + (index >> 4));
		if (!sector)
			return BOOT98_FS_IO_ERROR;
		raw = sector + (index & 15) * 32;
		if (!raw[0])
			return BOOT98_FS_NOT_FOUND;
		if (raw[0] == 0xe5 || raw[11] == 0x0f || raw[11] & 0x18)
			continue;
		if (!boot98_fat_name_matches(raw, canonical))
			continue;
		fat_file->first_cluster = boot98_fat_get16(raw + 26);
		file->size = boot98_fat_get32(raw + 28);
		return fat_file->first_cluster >= 2 ?
			BOOT98_FS_OK : BOOT98_FS_CORRUPT;
	}
	return BOOT98_FS_NOT_FOUND;
}

static enum boot98_fs_result fat16_read(
	struct boot98_file *file, uint64_t offset, void *buffer, uint32_t length,
	boot98_read_progress_t progress, void *progress_context)
{
	return boot98_fat_read_chain(file, offset, buffer, length, progress,
				     progress_context, fat16_next_cluster,
				     FAT16_END_OF_CHAIN);
}

static enum boot98_fs_result fat16_readdir(
	struct boot98_filesystem *filesystem, const char *path, unsigned wanted,
	struct boot98_dirent *entry)
{
	struct boot98_fat_state *fat = boot98_fat_state(filesystem);
	const uint8_t *sector = 0;
	unsigned visible = 0;

	if (*path && !(path[0] == '/' && !path[1]))
		return BOOT98_FS_INVALID_PATH;
	for (uint32_t index = 0; index < fat->root_entries; index++) {
		const uint8_t *raw;

		if (!(index & 15))
			sector = boot98_fat_read_sector(
				filesystem, fat->root_start + (index >> 4));
		if (!sector)
			return BOOT98_FS_IO_ERROR;
		raw = sector + (index & 15) * 32;
		if (!raw[0])
			return BOOT98_FS_NOT_FOUND;
		if (raw[0] == 0xe5 || raw[11] == 0x0f)
			continue;
		if (visible++ != wanted)
			continue;
		boot98_fat_decode_dirent(raw, entry);
		return BOOT98_FS_OK;
	}
	return BOOT98_FS_NOT_FOUND;
}

static enum boot98_fs_result fat16_contiguous_lba(
	struct boot98_file *file, uint32_t *absolute_lba)
{
	return boot98_fat_contiguous_lba(file, absolute_lba,
					 fat16_next_cluster);
}

const struct boot98_filesystem_driver boot98_fat16_driver = {
	.name = "fat16",
	.probe = fat16_probe,
	.mount = fat16_mount,
	.open = fat16_open,
	.read = fat16_read,
	.readdir = fat16_readdir,
	.contiguous_lba = fat16_contiguous_lba,
};
