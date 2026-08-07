/*
 * PC-9800 Bootloader FAT family support
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef BOOT98_FAT_H
#define BOOT98_FAT_H

#include "boot98-fs.h"

enum boot98_fat_type {
	BOOT98_FAT12 = 12,
	BOOT98_FAT16 = 16,
	BOOT98_FAT32 = 32,
};

/* All sector addresses below use the filesystem layer's physical 512-byte
 * units. bytes_per_sector and sector_scale retain the logical BPB geometry. */
struct boot98_fat_state {
	uint32_t fat_start;
	uint32_t root_start;
	uint32_t data_start;
	uint32_t total_sectors;
	uint32_t cluster_count;
	uint32_t fat_sectors;
	uint16_t bytes_per_sector;
	uint16_t root_entries;
	uint16_t sectors_per_cluster;
	uint8_t sector_scale;
	uint8_t number_of_fats;
	uint8_t type;
	uint8_t fat16_layout;
};

struct boot98_fat_file_state {
	uint32_t first_cluster;
};

typedef enum boot98_fs_result (*boot98_fat_next_cluster_t)(
	struct boot98_filesystem *filesystem, uint32_t cluster,
	uint32_t *next_cluster);

struct boot98_fat_state *boot98_fat_state(
	struct boot98_filesystem *filesystem);
struct boot98_fat_file_state *boot98_fat_file_state(
	struct boot98_file *file);

enum boot98_fs_result boot98_fat_probe(
	const struct boot98_volume *volume,
	enum boot98_fat_type required_type);
enum boot98_fs_result boot98_fat_mount(
	struct boot98_filesystem *filesystem,
	enum boot98_fat_type required_type);

const uint8_t *boot98_fat_read_sector(struct boot98_filesystem *filesystem,
				      uint32_t lba);
uint16_t boot98_fat_get16(const uint8_t *bytes);
uint32_t boot98_fat_get32(const uint8_t *bytes);

int boot98_fat_short_name(const char *path, char output[11]);
int boot98_fat_name_matches(const uint8_t entry[32], const char name[11]);
void boot98_fat_decode_dirent(const uint8_t raw[32],
			      struct boot98_dirent *entry);

enum boot98_fs_result boot98_fat_read_chain(
	struct boot98_file *file, uint64_t offset, void *buffer, uint32_t length,
	boot98_read_progress_t progress, void *progress_context,
	boot98_fat_next_cluster_t next_cluster, uint32_t end_of_chain);
enum boot98_fs_result boot98_fat_contiguous_lba(
	struct boot98_file *file, uint32_t *absolute_lba,
	boot98_fat_next_cluster_t next_cluster);

#endif
