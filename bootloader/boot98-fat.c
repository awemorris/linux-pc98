/*
 * PC-9800 Bootloader FAT family support
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "boot98-fat.h"

#define FAT_PROGRESS_INTERVAL (64U * 1024U)

/* BOOT98 keeps one filesystem mounted, so one shared physical-sector cache is
 * sufficient and avoids consuming stack or private-handle space. */
static uint8_t sector_cache[512];
static uint8_t bpb_cache[512];
static struct boot98_filesystem *sector_cache_owner;
static uint32_t sector_cache_lba;
static int sector_cache_valid;
static int sector_cache_dirty;

_Static_assert(sizeof(struct boot98_fat_state) <=
	       sizeof(((struct boot98_filesystem *)0)->private_data),
	       "FAT state exceeds generic filesystem storage");
_Static_assert(sizeof(struct boot98_fat_file_state) <=
	       sizeof(((struct boot98_file *)0)->private_data),
	       "FAT file state exceeds generic file storage");

static void copy_bytes(void *destination, const void *source, uint32_t length)
{
	uint8_t *output = destination;
	const uint8_t *input = source;

	while (length--)
		*output++ = *input++;
}

uint16_t boot98_fat_get16(const uint8_t *bytes)
{
	return bytes[0] | ((uint16_t)bytes[1] << 8);
}

uint32_t boot98_fat_get32(const uint8_t *bytes)
{
	return boot98_fat_get16(bytes) |
	       ((uint32_t)boot98_fat_get16(bytes + 2) << 16);
}

struct boot98_fat_state *boot98_fat_state(
	struct boot98_filesystem *filesystem)
{
	return (struct boot98_fat_state *)filesystem->private_data;
}

struct boot98_fat_file_state *boot98_fat_file_state(
	struct boot98_file *file)
{
	return (struct boot98_fat_file_state *)file->private_data;
}

const uint8_t *boot98_fat_read_sector(struct boot98_filesystem *filesystem,
				      uint32_t lba)
{
	const uint8_t *sector;

	if (boot98_fat_read_sector_result(filesystem, lba, &sector) !=
	    BOOT98_FS_OK)
		return 0;
	return sector;
}

enum boot98_fs_result boot98_fat_flush(
	struct boot98_filesystem *filesystem)
{
	enum boot98_fs_result result;

	if (!sector_cache_dirty || sector_cache_owner != filesystem)
		return BOOT98_FS_OK;
	result = boot98_volume_write_result(&filesystem->volume,
					    sector_cache_lba, sector_cache);
	if (result == BOOT98_FS_OK)
		sector_cache_dirty = 0;
	return result;
}

void boot98_fat_invalidate(struct boot98_filesystem *filesystem)
{
	if (sector_cache_owner == filesystem) {
		sector_cache_owner = 0;
		sector_cache_valid = 0;
		sector_cache_dirty = 0;
	}
}

enum boot98_fs_result boot98_fat_read_sector_result(
	struct boot98_filesystem *filesystem, uint32_t lba,
	const uint8_t **sector)
{
	enum boot98_fs_result result;
	struct boot98_fat_state *fat;

	if (!filesystem || !sector)
		return BOOT98_FS_INVALID_ARGUMENT;
	fat = boot98_fat_state(filesystem);
	if (lba >= fat->total_sectors)
		return BOOT98_FS_CORRUPT;
	if (sector_cache_owner == filesystem && sector_cache_valid &&
	    sector_cache_lba == lba) {
		*sector = sector_cache;
		return BOOT98_FS_OK;
	}
	if (sector_cache_dirty && sector_cache_owner != 0) {
		result = boot98_fat_flush(sector_cache_owner);
		if (result != BOOT98_FS_OK)
			return result;
	}
	sector_cache_owner = filesystem;
	sector_cache_valid = 0;
	sector_cache_dirty = 0;
	result = boot98_volume_read_result(&filesystem->volume, lba,
					  sector_cache);
	if (result != BOOT98_FS_OK)
		return result;
	sector_cache_lba = lba;
	sector_cache_valid = 1;
	*sector = sector_cache;
	return BOOT98_FS_OK;
}

enum boot98_fs_result boot98_fat_write_sector_result(
	struct boot98_filesystem *filesystem, uint32_t lba, uint8_t **sector)
{
	const uint8_t *read_sector;
	enum boot98_fs_result result;

	if (!filesystem || !sector)
		return BOOT98_FS_INVALID_ARGUMENT;
	if (!filesystem->volume.write)
		return BOOT98_FS_READ_ONLY;
	result = boot98_fat_read_sector_result(filesystem, lba, &read_sector);
	if (result != BOOT98_FS_OK)
		return result;
	*sector = (uint8_t *)read_sector;
	return BOOT98_FS_OK;
}

enum boot98_fs_result boot98_fat_mark_sector_dirty(
	struct boot98_filesystem *filesystem)
{
	if (!filesystem || !filesystem->volume.write)
		return BOOT98_FS_READ_ONLY;
	if (sector_cache_owner != filesystem || !sector_cache_valid)
		return BOOT98_FS_CORRUPT;
	sector_cache_dirty = 1;
	return BOOT98_FS_OK;
}

enum boot98_fs_result boot98_fat_cluster_lba(
	struct boot98_filesystem *filesystem, uint32_t cluster,
	uint32_t sector_in_cluster, uint32_t *lba)
{
	struct boot98_fat_state *fat;
	uint32_t cluster_offset;

	if (!filesystem || !lba)
		return BOOT98_FS_INVALID_ARGUMENT;
	fat = boot98_fat_state(filesystem);
	if (cluster < 2 || cluster >= fat->cluster_count + 2 ||
	    sector_in_cluster >= fat->sectors_per_cluster)
		return BOOT98_FS_CORRUPT;
	if (cluster - 2 >
	    (0xffffffffU - fat->data_start) / fat->sectors_per_cluster)
		return BOOT98_FS_CORRUPT;
	cluster_offset = fat->data_start +
			 (cluster - 2) * fat->sectors_per_cluster;
	if (sector_in_cluster > 0xffffffffU - cluster_offset)
		return BOOT98_FS_CORRUPT;
	*lba = cluster_offset + sector_in_cluster;
	if (*lba >= fat->total_sectors)
		return BOOT98_FS_CORRUPT;
	return BOOT98_FS_OK;
}

static enum boot98_fs_result parse_bpb(const struct boot98_volume *volume,
				       struct boot98_fat_state *fat)
{
	uint32_t reserved, fat_sectors, root_sectors, metadata;
	uint32_t total, total_physical, data_sectors;
	uint16_t bytes, fat16_sectors;
	uint8_t sectors_per_cluster;

	if (!boot98_volume_read(volume, 0, bpb_cache))
		return BOOT98_FS_IO_ERROR;
	bytes = boot98_fat_get16(bpb_cache + 11);
	fat->sector_scale = bytes == 512 ? 1 : bytes == 1024 ? 2 : 0;
	sectors_per_cluster = bpb_cache[13];
	reserved = boot98_fat_get16(bpb_cache + 14);
	fat->number_of_fats = bpb_cache[16];
	fat->root_entries = boot98_fat_get16(bpb_cache + 17);
	total = boot98_fat_get16(bpb_cache + 19);
	if (!total)
		total = boot98_fat_get32(bpb_cache + 32);
	fat16_sectors = boot98_fat_get16(bpb_cache + 22);
	fat_sectors = fat16_sectors;
	if (!fat_sectors)
		fat_sectors = boot98_fat_get32(bpb_cache + 36);
	if (!fat->sector_scale || !sectors_per_cluster || !reserved ||
	    !fat->number_of_fats || !fat_sectors || !total)
		return BOOT98_FS_CORRUPT;
	if (total > 0xffffffffU / fat->sector_scale ||
	    reserved > 0xffffffffU / fat->sector_scale ||
	    fat_sectors > 0xffffffffU / fat->sector_scale)
		return BOOT98_FS_CORRUPT;
	total_physical = total * fat->sector_scale;
	reserved *= fat->sector_scale;
	fat_sectors *= fat->sector_scale;
	fat->sectors_per_cluster = sectors_per_cluster * fat->sector_scale;
	root_sectors = ((uint32_t)fat->root_entries * 32 + 511) >> 9;
	if (fat_sectors > (0xffffffffU - reserved) / fat->number_of_fats)
		return BOOT98_FS_CORRUPT;
	metadata = reserved + fat_sectors * fat->number_of_fats;
	if (root_sectors > 0xffffffffU - metadata)
		return BOOT98_FS_CORRUPT;
	metadata += root_sectors;
	if (metadata >= total_physical)
		return BOOT98_FS_CORRUPT;
	data_sectors = total_physical - metadata;
	fat->cluster_count = data_sectors / fat->sectors_per_cluster;
	fat->type = fat->cluster_count < 4085 ? BOOT98_FAT12 :
	            fat->cluster_count < 65525 ? BOOT98_FAT16 : BOOT98_FAT32;
	fat->fat_start = reserved;
	fat->fat_sectors = fat_sectors;
	fat->root_start = reserved + fat_sectors * fat->number_of_fats;
	fat->data_start = fat->root_start + root_sectors;
	fat->total_sectors = total_physical;
	fat->bytes_per_sector = bytes;
	fat->fat16_layout = fat16_sectors != 0 && fat->root_entries != 0;
	return BOOT98_FS_OK;
}

enum boot98_fs_result boot98_fat_probe(
	const struct boot98_volume *volume, enum boot98_fat_type required_type)
{
	struct boot98_fat_state candidate = { 0 };
	enum boot98_fs_result result = parse_bpb(volume, &candidate);

	if (result != BOOT98_FS_OK)
		return result;
	if (candidate.type != required_type ||
	    (required_type == BOOT98_FAT16 && !candidate.fat16_layout))
		return BOOT98_FS_UNSUPPORTED;
	return BOOT98_FS_OK;
}

enum boot98_fs_result boot98_fat_mount(
	struct boot98_filesystem *filesystem, enum boot98_fat_type required_type)
{
	struct boot98_fat_state *fat = boot98_fat_state(filesystem);
	enum boot98_fs_result result = parse_bpb(&filesystem->volume, fat);

	if (result != BOOT98_FS_OK)
		return result;
	if (fat->type != required_type ||
	    (required_type == BOOT98_FAT16 && !fat->fat16_layout))
		return BOOT98_FS_UNSUPPORTED;
	fat->allocation_hint = 2;
	boot98_fat_invalidate(filesystem);
	return BOOT98_FS_OK;
}

int boot98_fat_short_name(const char *path, char output[11])
{
	unsigned base = 0, extension = 0;

	for (unsigned i = 0; i < 11; i++)
		output[i] = ' ';
	if (*path == '/')
		path++;
	if (!*path)
		return 0;
	while (*path && *path != '.') {
		char character = *path++;

		if (character == '/' || base == 8)
			return 0;
		output[base++] = character >= 'a' && character <= 'z' ?
		                 character - 32 : character;
	}
	if (!base)
		return 0;
	if (*path == '.')
		path++;
	while (*path) {
		char character = *path++;

		if (character == '/' || character == '.' || extension == 3)
			return 0;
		output[8 + extension++] =
			character >= 'a' && character <= 'z' ?
			character - 32 : character;
	}
	return 1;
}

int boot98_fat_name_matches(const uint8_t entry[32], const char name[11])
{
	for (unsigned i = 0; i < 11; i++)
		if (entry[i] != (uint8_t)name[i])
			return 0;
	return 1;
}

void boot98_fat_decode_dirent(const uint8_t raw[32],
			      struct boot98_dirent *entry)
{
	unsigned output = 0;

	for (unsigned i = 0; i < 8 && raw[i] != ' '; i++)
		entry->name[output++] = raw[i];
	if (raw[8] != ' ') {
		entry->name[output++] = '.';
		for (unsigned i = 8; i < 11 && raw[i] != ' '; i++)
			entry->name[output++] = raw[i];
	}
	entry->name[output] = 0;
	entry->size = boot98_fat_get32(raw + 28);
	entry->attributes = raw[11];
}

static int valid_cluster(uint32_t cluster, uint32_t end_of_chain)
{
	return cluster >= 2 && cluster < end_of_chain;
}

enum boot98_fs_result boot98_fat_read_chain(
	struct boot98_file *file, uint64_t offset, void *buffer, uint32_t length,
	boot98_read_progress_t progress, void *progress_context,
	boot98_fat_next_cluster_t next_cluster, uint32_t end_of_chain)
{
	struct boot98_filesystem *filesystem = file->filesystem;
	struct boot98_fat_state *fat = boot98_fat_state(filesystem);
	struct boot98_fat_file_state *fat_file = boot98_fat_file_state(file);
	uint32_t cluster = fat_file->first_cluster;
	uint32_t position, skip, within, since_update = 0;
	uint8_t *output = buffer;

	if (offset > 0xffffffffU || !next_cluster)
		return BOOT98_FS_INVALID_ARGUMENT;
	if (!valid_cluster(cluster, end_of_chain))
		return BOOT98_FS_CORRUPT;
	position = (uint32_t)offset;
	skip = position / 512;
	within = position & 511;
	while (skip >= fat->sectors_per_cluster) {
		enum boot98_fs_result result = next_cluster(filesystem, cluster,
							     &cluster);

		if (result != BOOT98_FS_OK)
			return result;
		if (!valid_cluster(cluster, end_of_chain))
			return BOOT98_FS_CORRUPT;
		skip -= fat->sectors_per_cluster;
	}
	while (length) {
		uint32_t lba;
		uint32_t chunk = 512 - within;
		const uint8_t *input;
		enum boot98_fs_result result;

		result = boot98_fat_cluster_lba(filesystem, cluster, skip, &lba);
		if (result != BOOT98_FS_OK)
			return result;
		result = boot98_fat_read_sector_result(filesystem, lba, &input);
		if (result != BOOT98_FS_OK)
			return result;
		if (chunk > length)
			chunk = length;
		copy_bytes(output, input + within, chunk);
		output += chunk;
		length -= chunk;
		if (progress) {
			since_update += chunk;
			if (since_update >= FAT_PROGRESS_INTERVAL || !length) {
				progress(progress_context, since_update);
				since_update = 0;
			}
		}
		within = 0;
		if (++skip >= fat->sectors_per_cluster && length) {
			enum boot98_fs_result result;

			skip = 0;
			result = next_cluster(filesystem, cluster, &cluster);
			if (result != BOOT98_FS_OK)
				return result;
			if (!valid_cluster(cluster, end_of_chain))
				return BOOT98_FS_CORRUPT;
		}
	}
	return BOOT98_FS_OK;
}

enum boot98_fs_result boot98_fat_contiguous_lba(
	struct boot98_file *file, uint32_t *absolute_lba,
	boot98_fat_next_cluster_t next_cluster)
{
	struct boot98_filesystem *filesystem = file->filesystem;
	struct boot98_fat_state *fat = boot98_fat_state(filesystem);
	struct boot98_fat_file_state *fat_file = boot98_fat_file_state(file);
	uint32_t cluster = fat_file->first_cluster;
	uint32_t cluster_lba;
	uint64_t left = file->size;

	if (!next_cluster)
		return BOOT98_FS_INVALID_ARGUMENT;
	if (cluster < 2)
		return BOOT98_FS_CORRUPT;
	while (left > (uint32_t)fat->sectors_per_cluster * 512) {
		uint32_t next;
		enum boot98_fs_result result = next_cluster(filesystem, cluster,
							     &next);

		if (result != BOOT98_FS_OK)
			return result;
		if (next != cluster + 1)
			return BOOT98_FS_UNSUPPORTED;
		cluster = next;
		left -= (uint32_t)fat->sectors_per_cluster * 512;
	}
	if (boot98_fat_cluster_lba(filesystem, fat_file->first_cluster, 0,
				   &cluster_lba) != BOOT98_FS_OK)
		return BOOT98_FS_CORRUPT;
	if (cluster_lba > 0xffffffffU - filesystem->volume.start_lba)
		return BOOT98_FS_CORRUPT;
	*absolute_lba = filesystem->volume.start_lba + cluster_lba;
	return BOOT98_FS_OK;
}
