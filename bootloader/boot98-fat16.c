/*
 * PC-9800 Bootloader FAT16 driver
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "boot98-fat.h"
#include "boot98-fat16.h"

#define FAT16_RESERVED_CLUSTER 0xfff0U
#define FAT16_END_OF_CHAIN 0xffffU
#define FAT16_DIRECTORY_ENTRY_SIZE 32U
#define FAT16_ENTRIES_PER_SECTOR (512U / FAT16_DIRECTORY_ENTRY_SIZE)

static void copy_bytes(void *destination, const void *source, uint32_t length)
{
	uint8_t *output = destination;
	const uint8_t *input = source;

	while (length--)
		*output++ = *input++;
}

static void clear_bytes(void *destination, uint32_t length)
{
	uint8_t *output = destination;

	while (length--)
		*output++ = 0;
}

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

static enum boot98_fs_result fat16_probe(const struct boot98_volume *volume)
{
	return boot98_fat_probe(volume, BOOT98_FAT16);
}

static enum boot98_fs_result fat16_mount(
	struct boot98_filesystem *filesystem)
{
	struct boot98_fat_state *fat;
	enum boot98_fs_result result;
	uint32_t fat_entries;

	result = boot98_fat_mount(filesystem, BOOT98_FAT16);
	if (result != BOOT98_FS_OK)
		return result;
	fat = boot98_fat_state(filesystem);
	if (!fat->root_entries || !fat->fat_sectors ||
	    fat->fat_sectors > 0xffffffffU / 512U)
		return BOOT98_FS_CORRUPT;
	fat_entries = fat->fat_sectors * 512U / 2U;
	if (fat_entries < fat->cluster_count + 2U ||
	    fat->cluster_count + 2U >= FAT16_RESERVED_CLUSTER)
		return BOOT98_FS_CORRUPT;
	return BOOT98_FS_OK;
}

static int fat16_valid_cluster(const struct boot98_fat_state *fat,
			       uint32_t cluster)
{
	return cluster >= 2U && cluster < fat->cluster_count + 2U;
}

static int fat16_is_end(uint32_t cluster)
{
	return cluster >= 0xfff8U;
}

static enum boot98_fs_result fat16_next_cluster(
	struct boot98_filesystem *filesystem, uint32_t cluster,
	uint32_t *next_cluster)
{
	struct boot98_fat_state *fat = boot98_fat_state(filesystem);
	uint32_t offset;
	const uint8_t *sector;
	enum boot98_fs_result result;

	if (!next_cluster || !fat16_valid_cluster(fat, cluster))
		return BOOT98_FS_CORRUPT;
	offset = cluster * 2U;
	result = boot98_fat_read_sector_result(filesystem,
					       fat->fat_start + (offset >> 9),
					       &sector);
	if (result != BOOT98_FS_OK)
		return result;
	*next_cluster = boot98_fat_get16(sector + (offset & 511U));
	return BOOT98_FS_OK;
}

static enum boot98_fs_result fat16_set_cluster(
	struct boot98_filesystem *filesystem, uint32_t cluster, uint16_t value)
{
	struct boot98_fat_state *fat = boot98_fat_state(filesystem);
	uint32_t offset;
	unsigned copy;

	if (!fat16_valid_cluster(fat, cluster))
		return BOOT98_FS_CORRUPT;
	offset = cluster * 2U;
	for (copy = 0; copy < fat->number_of_fats; copy++) {
		uint32_t copy_start, lba;
		uint8_t *sector;
		enum boot98_fs_result result;

		if (copy > (0xffffffffU - fat->fat_start) / fat->fat_sectors)
			return BOOT98_FS_CORRUPT;
		copy_start = fat->fat_start + copy * fat->fat_sectors;
		if ((offset >> 9) > 0xffffffffU - copy_start)
			return BOOT98_FS_CORRUPT;
		lba = copy_start + (offset >> 9);
		result = boot98_fat_write_sector_result(filesystem, lba, &sector);
		if (result != BOOT98_FS_OK)
			return result;
		put16(sector + (offset & 511U), value);
		result = boot98_fat_mark_sector_dirty(filesystem);
		if (result == BOOT98_FS_OK)
			result = boot98_fat_flush(filesystem);
		if (result != BOOT98_FS_OK)
			return result;
	}
	return BOOT98_FS_OK;
}

static enum boot98_fs_result fat16_validate_chain(
	struct boot98_filesystem *filesystem, uint32_t first_cluster)
{
	struct boot98_fat_state *fat = boot98_fat_state(filesystem);
	uint32_t slow = first_cluster, fast = first_cluster;
	uint32_t steps;

	if (!fat16_valid_cluster(fat, first_cluster))
		return BOOT98_FS_CORRUPT;
	for (steps = 0; steps <= fat->cluster_count; steps++) {
		uint32_t next;
		enum boot98_fs_result result;

		result = fat16_next_cluster(filesystem, slow, &next);
		if (result != BOOT98_FS_OK)
			return result;
		if (fat16_is_end(next))
			return BOOT98_FS_OK;
		if (!fat16_valid_cluster(fat, next))
			return BOOT98_FS_CORRUPT;
		slow = next;

		result = fat16_next_cluster(filesystem, fast, &next);
		if (result != BOOT98_FS_OK)
			return result;
		if (fat16_is_end(next))
			return BOOT98_FS_OK;
		if (!fat16_valid_cluster(fat, next))
			return BOOT98_FS_CORRUPT;
		fast = next;
		result = fat16_next_cluster(filesystem, fast, &next);
		if (result != BOOT98_FS_OK)
			return result;
		if (fat16_is_end(next))
			return BOOT98_FS_OK;
		if (!fat16_valid_cluster(fat, next))
			return BOOT98_FS_CORRUPT;
		fast = next;
		if (slow == fast)
			return BOOT98_FS_CORRUPT;
	}
	return BOOT98_FS_CORRUPT;
}

static enum boot98_fs_result fat16_free_chain(
	struct boot98_filesystem *filesystem, uint32_t first_cluster)
{
	struct boot98_fat_state *fat = boot98_fat_state(filesystem);
	uint32_t cluster = first_cluster;
	uint32_t steps;
	enum boot98_fs_result result;

	if (!first_cluster)
		return BOOT98_FS_OK;
	result = fat16_validate_chain(filesystem, first_cluster);
	if (result != BOOT98_FS_OK)
		return result;
	for (steps = 0; steps < fat->cluster_count; steps++) {
		uint32_t next;

		result = fat16_next_cluster(filesystem, cluster, &next);
		if (result != BOOT98_FS_OK)
			return result;
		result = fat16_set_cluster(filesystem, cluster, 0);
		if (result != BOOT98_FS_OK)
			return result;
		if (fat16_is_end(next))
			return BOOT98_FS_OK;
		cluster = next;
	}
	return BOOT98_FS_CORRUPT;
}

static enum boot98_fs_result fat16_find_free_cluster(
	struct boot98_filesystem *filesystem, uint32_t *free_cluster)
{
	struct boot98_fat_state *fat = boot98_fat_state(filesystem);
	uint32_t start = fat->allocation_hint;
	uint32_t index;

	if (!free_cluster || !fat->cluster_count)
		return BOOT98_FS_CORRUPT;
	if (!fat16_valid_cluster(fat, start))
		start = 2;
	for (index = 0; index < fat->cluster_count; index++) {
		uint32_t cluster = 2U +
			((start - 2U + index) % fat->cluster_count);
		uint32_t value;
		enum boot98_fs_result result;

		result = fat16_next_cluster(filesystem, cluster, &value);
		if (result != BOOT98_FS_OK)
			return result;
		if (!value) {
			*free_cluster = cluster;
			fat->allocation_hint = cluster + 1U;
			if (!fat16_valid_cluster(fat, fat->allocation_hint))
				fat->allocation_hint = 2;
			return BOOT98_FS_OK;
		}
	}
	return BOOT98_FS_NO_SPACE;
}

static enum boot98_fs_result fat16_zero_cluster(
	struct boot98_filesystem *filesystem, uint32_t cluster)
{
	struct boot98_fat_state *fat = boot98_fat_state(filesystem);
	uint32_t index;

	for (index = 0; index < fat->sectors_per_cluster; index++) {
		uint32_t lba;
		uint8_t *sector;
		enum boot98_fs_result result;

		result = boot98_fat_cluster_lba(filesystem, cluster, index, &lba);
		if (result != BOOT98_FS_OK)
			return result;
		result = boot98_fat_write_sector_result(filesystem, lba, &sector);
		if (result != BOOT98_FS_OK)
			return result;
		clear_bytes(sector, 512);
		result = boot98_fat_mark_sector_dirty(filesystem);
		if (result == BOOT98_FS_OK)
			result = boot98_fat_flush(filesystem);
		if (result != BOOT98_FS_OK)
			return result;
	}
	return BOOT98_FS_OK;
}

static enum boot98_fs_result fat16_allocate_cluster(
	struct boot98_filesystem *filesystem, uint32_t *cluster)
{
	enum boot98_fs_result result;

	result = fat16_find_free_cluster(filesystem, cluster);
	if (result != BOOT98_FS_OK)
		return result;
	result = fat16_zero_cluster(filesystem, *cluster);
	if (result != BOOT98_FS_OK)
		return result;
	return fat16_set_cluster(filesystem, *cluster, FAT16_END_OF_CHAIN);
}

static enum boot98_fs_result fat16_find_entry(
	struct boot98_filesystem *filesystem, const char canonical[11],
	uint32_t *entry_lba, uint16_t *entry_offset,
	uint32_t *free_lba, uint16_t *free_offset)
{
	struct boot98_fat_state *fat = boot98_fat_state(filesystem);
	uint32_t index;
	int have_free = 0;

	for (index = 0; index < fat->root_entries; index++) {
		uint32_t lba = fat->root_start + index / FAT16_ENTRIES_PER_SECTOR;
		uint16_t offset = (uint16_t)((index % FAT16_ENTRIES_PER_SECTOR) *
					     FAT16_DIRECTORY_ENTRY_SIZE);
		const uint8_t *sector;
		const uint8_t *raw;
		enum boot98_fs_result result;

		result = boot98_fat_read_sector_result(filesystem, lba, &sector);
		if (result != BOOT98_FS_OK)
			return result;
		raw = sector + offset;
		if ((raw[0] == 0 || raw[0] == 0xe5) && !have_free) {
			*free_lba = lba;
			*free_offset = offset;
			have_free = 1;
		}
		if (!raw[0])
			break;
		if (raw[0] == 0xe5 || raw[11] == 0x0f || raw[11] & 0x18)
			continue;
		if (boot98_fat_name_matches(raw, canonical)) {
			*entry_lba = lba;
			*entry_offset = offset;
			return BOOT98_FS_OK;
		}
	}
	return have_free ? BOOT98_FS_NOT_FOUND : BOOT98_FS_NO_SPACE;
}

static enum boot98_fs_result fat16_populate_file(
	struct boot98_file *file, uint32_t lba, uint16_t offset,
	const uint8_t raw[32])
{
	struct boot98_fat_state *fat = boot98_fat_state(file->filesystem);
	struct boot98_fat_file_state *state = boot98_fat_file_state(file);

	state->first_cluster = boot98_fat_get16(raw + 26);
	state->directory_lba = lba;
	state->directory_offset = offset;
	state->directory_dirty = 0;
	file->size = boot98_fat_get32(raw + 28);
	if (!file->size && !state->first_cluster)
		return BOOT98_FS_OK;
	return fat16_valid_cluster(fat, state->first_cluster) ?
		BOOT98_FS_OK : BOOT98_FS_CORRUPT;
}

static enum boot98_fs_result fat16_open(
	struct boot98_filesystem *filesystem, const char *path,
	struct boot98_file *file)
{
	char canonical[11];
	uint32_t lba = 0, free_lba = 0;
	uint16_t offset = 0, free_offset = 0;
	const uint8_t *sector;
	enum boot98_fs_result result;

	if (!boot98_fat_short_name(path, canonical))
		return BOOT98_FS_INVALID_PATH;
	result = fat16_find_entry(filesystem, canonical, &lba, &offset,
				  &free_lba, &free_offset);
	if (result != BOOT98_FS_OK)
		return result == BOOT98_FS_NO_SPACE ? BOOT98_FS_NOT_FOUND : result;
	result = boot98_fat_read_sector_result(filesystem, lba, &sector);
	if (result != BOOT98_FS_OK)
		return result;
	return fat16_populate_file(file, lba, offset, sector + offset);
}

static enum boot98_fs_result fat16_flush_file(struct boot98_file *file)
{
	struct boot98_fat_file_state *state = boot98_fat_file_state(file);
	uint8_t *sector;
	enum boot98_fs_result result;

	if (!file->filesystem->volume.write)
		return BOOT98_FS_READ_ONLY;
	result = boot98_fat_flush(file->filesystem);
	if (result != BOOT98_FS_OK || !state->directory_dirty)
		return result;
	if (file->size > 0xffffffffU)
		return BOOT98_FS_INVALID_ARGUMENT;
	result = boot98_fat_write_sector_result(file->filesystem,
						state->directory_lba, &sector);
	if (result != BOOT98_FS_OK)
		return result;
	put16(sector + state->directory_offset + 26,
	      (uint16_t)state->first_cluster);
	put32(sector + state->directory_offset + 28, (uint32_t)file->size);
	result = boot98_fat_mark_sector_dirty(file->filesystem);
	if (result == BOOT98_FS_OK)
		result = boot98_fat_flush(file->filesystem);
	if (result == BOOT98_FS_OK)
		state->directory_dirty = 0;
	return result;
}

static enum boot98_fs_result fat16_cluster_at(
	struct boot98_file *file, uint32_t cluster_index, int allocate,
	uint32_t *found_cluster)
{
	struct boot98_fat_state *fat = boot98_fat_state(file->filesystem);
	struct boot98_fat_file_state *state = boot98_fat_file_state(file);
	uint32_t cluster = state->first_cluster;
	uint32_t index;
	enum boot98_fs_result result;

	if (!cluster) {
		if (!allocate)
			return BOOT98_FS_CORRUPT;
		result = fat16_allocate_cluster(file->filesystem, &cluster);
		if (result != BOOT98_FS_OK)
			return result;
		state->first_cluster = cluster;
		state->directory_dirty = 1;
	}
	if (!fat16_valid_cluster(fat, cluster))
		return BOOT98_FS_CORRUPT;
	for (index = 0; index < cluster_index; index++) {
		uint32_t next;

		if (index >= fat->cluster_count)
			return BOOT98_FS_CORRUPT;
		result = fat16_next_cluster(file->filesystem, cluster, &next);
		if (result != BOOT98_FS_OK)
			return result;
		if (fat16_is_end(next)) {
			if (!allocate)
				return BOOT98_FS_CORRUPT;
			result = fat16_allocate_cluster(file->filesystem, &next);
			if (result != BOOT98_FS_OK)
				return result;
			result = fat16_set_cluster(file->filesystem, cluster,
						   (uint16_t)next);
			if (result != BOOT98_FS_OK)
				return result;
		} else if (!fat16_valid_cluster(fat, next)) {
			return BOOT98_FS_CORRUPT;
		}
		cluster = next;
	}
	*found_cluster = cluster;
	return BOOT98_FS_OK;
}

static enum boot98_fs_result fat16_write_bytes(
	struct boot98_file *file, uint32_t offset, const uint8_t *input,
	uint32_t length, int zero)
{
	struct boot98_fat_state *fat = boot98_fat_state(file->filesystem);
	uint32_t cluster_bytes = (uint32_t)fat->sectors_per_cluster * 512U;
	uint32_t position = offset;

	while (length) {
		uint32_t cluster_index = position / cluster_bytes;
		uint32_t in_cluster = position % cluster_bytes;
		uint32_t sector_index = in_cluster / 512U;
		uint32_t within = in_cluster & 511U;
		uint32_t chunk = 512U - within;
		uint32_t cluster, lba;
		uint8_t *sector;
		enum boot98_fs_result result;

		if (chunk > length)
			chunk = length;
		result = fat16_cluster_at(file, cluster_index, 1, &cluster);
		if (result != BOOT98_FS_OK)
			return result;
		result = boot98_fat_cluster_lba(file->filesystem, cluster,
						sector_index, &lba);
		if (result != BOOT98_FS_OK)
			return result;
		result = boot98_fat_write_sector_result(file->filesystem, lba,
						  &sector);
		if (result != BOOT98_FS_OK)
			return result;
		if (zero)
			clear_bytes(sector + within, chunk);
		else
			copy_bytes(sector + within, input, chunk);
		result = boot98_fat_mark_sector_dirty(file->filesystem);
		if (result == BOOT98_FS_OK)
			result = boot98_fat_flush(file->filesystem);
		if (result != BOOT98_FS_OK)
			return result;
		if (!zero)
			input += chunk;
		position += chunk;
		length -= chunk;
	}
	return BOOT98_FS_OK;
}

static enum boot98_fs_result fat16_write(
	struct boot98_file *file, uint64_t offset, const void *buffer,
	uint32_t length)
{
	uint64_t end;
	enum boot98_fs_result result;

	if (!file->filesystem->volume.write)
		return BOOT98_FS_READ_ONLY;
	if ((!buffer && length) || offset > 0xffffffffU ||
	    (uint64_t)length > 0xffffffffU - offset)
		return BOOT98_FS_INVALID_ARGUMENT;
	if (!length)
		return BOOT98_FS_OK;
	if (boot98_fat_file_state(file)->first_cluster) {
		result = fat16_validate_chain(file->filesystem,
			boot98_fat_file_state(file)->first_cluster);
		if (result != BOOT98_FS_OK)
			return result;
	}
	end = offset + length;
	if (offset > file->size) {
		result = fat16_write_bytes(file, (uint32_t)file->size, 0,
					   (uint32_t)(offset - file->size), 1);
		if (result != BOOT98_FS_OK)
			return result;
	}
	result = fat16_write_bytes(file, (uint32_t)offset, buffer, length, 0);
	if (result != BOOT98_FS_OK)
		return result;
	if (end > file->size) {
		file->size = end;
		boot98_fat_file_state(file)->directory_dirty = 1;
	}
	return BOOT98_FS_OK;
}

static enum boot98_fs_result fat16_truncate(
	struct boot98_file *file, uint64_t size)
{
	struct boot98_fat_state *fat = boot98_fat_state(file->filesystem);
	struct boot98_fat_file_state *state = boot98_fat_file_state(file);
	uint32_t old_first = state->first_cluster;
	uint64_t old_size = file->size;
	enum boot98_fs_result result;

	if (!file->filesystem->volume.write)
		return BOOT98_FS_READ_ONLY;
	if (size > 0xffffffffU)
		return BOOT98_FS_INVALID_ARGUMENT;
	/* A zero-length file may still own a cluster chain.  Creating an
	 * existing file has truncate semantics and must release that chain. */
	if (size == file->size && (size || !state->first_cluster))
		return BOOT98_FS_OK;
	if (state->first_cluster) {
		result = fat16_validate_chain(file->filesystem,
					      state->first_cluster);
		if (result != BOOT98_FS_OK)
			return result;
	}
	if (size > file->size) {
		result = fat16_write_bytes(file, (uint32_t)file->size, 0,
					   (uint32_t)(size - file->size), 1);
		if (result != BOOT98_FS_OK)
			return result;
		file->size = size;
		state->directory_dirty = 1;
		return BOOT98_FS_OK;
	}
	if (!size) {
		state->first_cluster = 0;
		file->size = 0;
		state->directory_dirty = 1;
		result = fat16_flush_file(file);
		if (result != BOOT98_FS_OK) {
			state->first_cluster = old_first;
			file->size = old_size;
			state->directory_dirty = 1;
			return result;
		}
		return fat16_free_chain(file->filesystem, old_first);
	}
	{
		uint32_t cluster_bytes = (uint32_t)fat->sectors_per_cluster * 512U;
		uint32_t keep_index = ((uint32_t)size - 1U) / cluster_bytes;
		uint32_t keep, tail;

		result = fat16_cluster_at(file, keep_index, 0, &keep);
		if (result != BOOT98_FS_OK)
			return result;
		result = fat16_next_cluster(file->filesystem, keep, &tail);
		if (result != BOOT98_FS_OK)
			return result;
		file->size = size;
		state->directory_dirty = 1;
		result = fat16_flush_file(file);
		if (result != BOOT98_FS_OK)
			return result;
		if (fat16_is_end(tail))
			return BOOT98_FS_OK;
		if (!fat16_valid_cluster(fat, tail))
			return BOOT98_FS_CORRUPT;
		result = fat16_set_cluster(file->filesystem, keep,
					   FAT16_END_OF_CHAIN);
		if (result != BOOT98_FS_OK)
			return result;
		return fat16_free_chain(file->filesystem, tail);
	}
}

static enum boot98_fs_result fat16_create(
	struct boot98_filesystem *filesystem, const char *path,
	struct boot98_file *file)
{
	char canonical[11];
	uint32_t lba = 0, free_lba = 0;
	uint16_t offset = 0, free_offset = 0;
	uint8_t *sector;
	enum boot98_fs_result result;

	if (!filesystem->volume.write)
		return BOOT98_FS_READ_ONLY;
	if (!boot98_fat_short_name(path, canonical))
		return BOOT98_FS_INVALID_PATH;
	result = fat16_find_entry(filesystem, canonical, &lba, &offset,
				  &free_lba, &free_offset);
	if (result == BOOT98_FS_OK) {
		const uint8_t *read_sector;

		result = boot98_fat_read_sector_result(filesystem, lba,
						       &read_sector);
		if (result != BOOT98_FS_OK)
			return result;
		result = fat16_populate_file(file, lba, offset,
					     read_sector + offset);
		if (result != BOOT98_FS_OK)
			return result;
		return fat16_truncate(file, 0);
	}
	if (result != BOOT98_FS_NOT_FOUND)
		return result;
	result = boot98_fat_write_sector_result(filesystem, free_lba, &sector);
	if (result != BOOT98_FS_OK)
		return result;
	clear_bytes(sector + free_offset, 32);
	copy_bytes(sector + free_offset, canonical, 11);
	sector[free_offset + 11] = 0x20;
	result = boot98_fat_mark_sector_dirty(filesystem);
	if (result == BOOT98_FS_OK)
		result = boot98_fat_flush(filesystem);
	if (result != BOOT98_FS_OK)
		return result;
	return fat16_populate_file(file, free_lba, free_offset,
				   sector + free_offset);
}

static enum boot98_fs_result fat16_read(
	struct boot98_file *file, uint64_t offset, void *buffer, uint32_t length,
	boot98_read_progress_t progress, void *progress_context)
{
	return boot98_fat_read_chain(file, offset, buffer, length, progress,
				     progress_context, fat16_next_cluster,
				     FAT16_RESERVED_CLUSTER);
}

static enum boot98_fs_result fat16_readdir(
	struct boot98_filesystem *filesystem, const char *path, unsigned wanted,
	struct boot98_dirent *entry)
{
	struct boot98_fat_state *fat = boot98_fat_state(filesystem);
	unsigned visible = 0;
	uint32_t index;

	if (*path && !(path[0] == '/' && !path[1]))
		return BOOT98_FS_INVALID_PATH;
	for (index = 0; index < fat->root_entries; index++) {
		uint32_t lba = fat->root_start + index / FAT16_ENTRIES_PER_SECTOR;
		uint16_t offset = (uint16_t)((index % FAT16_ENTRIES_PER_SECTOR) * 32U);
		const uint8_t *sector;
		const uint8_t *raw;
		enum boot98_fs_result result;

		result = boot98_fat_read_sector_result(filesystem, lba, &sector);
		if (result != BOOT98_FS_OK)
			return result;
		raw = sector + offset;
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

static enum boot98_fs_result fat16_stat(
	struct boot98_filesystem *filesystem, const char *path,
	struct boot98_dirent *entry)
{
	char canonical[11];
	uint32_t lba = 0, free_lba = 0;
	uint16_t offset = 0, free_offset = 0;
	const uint8_t *sector;
	enum boot98_fs_result result;

	if (!boot98_fat_short_name(path, canonical))
		return BOOT98_FS_INVALID_PATH;
	result = fat16_find_entry(filesystem, canonical, &lba, &offset,
				  &free_lba, &free_offset);
	if (result != BOOT98_FS_OK)
		return result == BOOT98_FS_NO_SPACE ? BOOT98_FS_NOT_FOUND : result;
	result = boot98_fat_read_sector_result(filesystem, lba, &sector);
	if (result != BOOT98_FS_OK)
		return result;
	boot98_fat_decode_dirent(sector + offset, entry);
	return BOOT98_FS_OK;
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
	.create = fat16_create,
	.open = fat16_open,
	.read = fat16_read,
	.write = fat16_write,
	.truncate = fat16_truncate,
	.flush = fat16_flush_file,
	.readdir = fat16_readdir,
	.stat = fat16_stat,
	.contiguous_lba = fat16_contiguous_lba,
};
