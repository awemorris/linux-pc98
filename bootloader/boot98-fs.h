/*
 * PC-9800 Bootloader filesystem interface
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef BOOT98_FS_H
#define BOOT98_FS_H

#include <stdint.h>

#define BOOT98_PATH_MAX 256
#define BOOT98_FS_PRIVATE_WORDS 16
#define BOOT98_FILE_PRIVATE_WORDS 8

struct boot98_volume;
struct boot98_filesystem;
struct boot98_file;

typedef int (*boot98_volume_read_t)(const void *context, uint32_t lba,
				    void *buffer);
typedef void (*boot98_read_progress_t)(void *context, uint32_t bytes);

/* A partition-sized view of a BIOS block device. LBA values passed to read()
 * are relative to start_lba; the callback itself receives absolute LBAs. */
struct boot98_volume {
	const void *context;
	uint32_t start_lba;
	uint16_t sector_size;
	boot98_volume_read_t read;
};

struct boot98_dirent {
	char name[BOOT98_PATH_MAX];
	uint64_t size;
	uint8_t attributes;
};

struct boot98_filesystem_driver {
	const char *name;
	int (*probe)(const struct boot98_volume *volume);
	int (*mount)(struct boot98_filesystem *filesystem);
	int (*open)(struct boot98_filesystem *filesystem, const char *path,
		    struct boot98_file *file);
	int (*read)(struct boot98_file *file, uint64_t offset, void *buffer,
		    uint32_t length, boot98_read_progress_t progress,
		    void *progress_context);
	int (*readdir)(struct boot98_filesystem *filesystem, const char *path,
		       unsigned index, struct boot98_dirent *entry);
	int (*contiguous_lba)(struct boot98_file *file, uint32_t *absolute_lba);
};

struct boot98_filesystem {
	const struct boot98_filesystem_driver *driver;
	struct boot98_volume volume;
	uint32_t private_data[BOOT98_FS_PRIVATE_WORDS];
};

struct boot98_file {
	struct boot98_filesystem *filesystem;
	uint64_t size;
	uint32_t private_data[BOOT98_FILE_PRIVATE_WORDS];
};

int boot98_volume_read(const struct boot98_volume *volume, uint32_t lba,
		       void *buffer);
int boot98_fs_mount(struct boot98_filesystem *filesystem,
		    const struct boot98_volume *volume,
		    const struct boot98_filesystem_driver *const *drivers,
		    unsigned driver_count);
void boot98_fs_reset(struct boot98_filesystem *filesystem);
int boot98_fs_open(struct boot98_filesystem *filesystem, const char *path,
		   struct boot98_file *file);
int boot98_file_read(struct boot98_file *file, uint64_t offset, void *buffer,
		     uint32_t length);
int boot98_file_read_progress(struct boot98_file *file, uint64_t offset,
			      void *buffer, uint32_t length,
			      boot98_read_progress_t progress,
			      void *progress_context);
int boot98_fs_readdir(struct boot98_filesystem *filesystem, const char *path,
		      unsigned index, struct boot98_dirent *entry);
int boot98_file_contiguous_lba(struct boot98_file *file,
			       uint32_t *absolute_lba);

#endif
