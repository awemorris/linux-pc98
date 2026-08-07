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
typedef int (*boot98_volume_write_t)(void *context, uint32_t lba,
				     const void *buffer);
typedef void (*boot98_read_progress_t)(void *context, uint32_t bytes);

/* Stable internal results.  The public Boolean entry points below are kept as
 * compatibility wrappers while BOOT.SYS callers are migrated incrementally. */
enum boot98_fs_result {
	BOOT98_FS_OK = 0,
	BOOT98_FS_NOT_FOUND,
	BOOT98_FS_INVALID_PATH,
	BOOT98_FS_READ_ONLY,
	BOOT98_FS_NO_SPACE,
	BOOT98_FS_IO_ERROR,
	BOOT98_FS_CORRUPT,
	BOOT98_FS_UNSUPPORTED,
	BOOT98_FS_INVALID_ARGUMENT,
};

/* A partition-sized view of a BIOS block device. LBA values passed to the
 * generic helpers are relative to start_lba; callbacks receive absolute
 * physical LBAs.  A NULL write callback makes the volume read-only. */
struct boot98_volume {
	void *context;
	uint32_t start_lba;
	uint16_t sector_size;
	boot98_volume_read_t read;
	boot98_volume_write_t write;
};

struct boot98_dirent {
	char name[BOOT98_PATH_MAX];
	uint64_t size;
	uint8_t attributes;
};

struct boot98_filesystem_driver {
	const char *name;
	enum boot98_fs_result (*probe)(const struct boot98_volume *volume);
	enum boot98_fs_result (*mount)(struct boot98_filesystem *filesystem);
	enum boot98_fs_result (*create)(struct boot98_filesystem *filesystem,
					const char *path,
					struct boot98_file *file);
	enum boot98_fs_result (*open)(struct boot98_filesystem *filesystem,
				      const char *path,
				      struct boot98_file *file);
	enum boot98_fs_result (*read)(struct boot98_file *file, uint64_t offset,
				      void *buffer, uint32_t length,
				      boot98_read_progress_t progress,
				      void *progress_context);
	enum boot98_fs_result (*write)(struct boot98_file *file, uint64_t offset,
				       const void *buffer, uint32_t length);
	enum boot98_fs_result (*truncate)(struct boot98_file *file, uint64_t size);
	enum boot98_fs_result (*flush)(struct boot98_file *file);
	enum boot98_fs_result (*readdir)(struct boot98_filesystem *filesystem,
					 const char *path, unsigned index,
					 struct boot98_dirent *entry);
	enum boot98_fs_result (*stat)(struct boot98_filesystem *filesystem,
				      const char *path,
				      struct boot98_dirent *entry);
	enum boot98_fs_result (*contiguous_lba)(struct boot98_file *file,
						uint32_t *absolute_lba);
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
enum boot98_fs_result boot98_volume_read_result(
	const struct boot98_volume *volume, uint32_t lba, void *buffer);
int boot98_volume_write(struct boot98_volume *volume, uint32_t lba,
			const void *buffer);
enum boot98_fs_result boot98_volume_write_result(
	struct boot98_volume *volume, uint32_t lba, const void *buffer);
int boot98_fs_mount(struct boot98_filesystem *filesystem,
		    const struct boot98_volume *volume,
		    const struct boot98_filesystem_driver *const *drivers,
		    unsigned driver_count);
enum boot98_fs_result boot98_fs_mount_result(
	struct boot98_filesystem *filesystem, const struct boot98_volume *volume,
	const struct boot98_filesystem_driver *const *drivers,
	unsigned driver_count);
void boot98_fs_reset(struct boot98_filesystem *filesystem);
int boot98_fs_open(struct boot98_filesystem *filesystem, const char *path,
		   struct boot98_file *file);
enum boot98_fs_result boot98_fs_open_result(
	struct boot98_filesystem *filesystem, const char *path,
	struct boot98_file *file);
enum boot98_fs_result boot98_fs_create_result(
	struct boot98_filesystem *filesystem, const char *path,
	struct boot98_file *file);
int boot98_file_read(struct boot98_file *file, uint64_t offset, void *buffer,
		     uint32_t length);
int boot98_file_read_progress(struct boot98_file *file, uint64_t offset,
			      void *buffer, uint32_t length,
			      boot98_read_progress_t progress,
			      void *progress_context);
enum boot98_fs_result boot98_file_read_result(
	struct boot98_file *file, uint64_t offset, void *buffer, uint32_t length,
	boot98_read_progress_t progress, void *progress_context);
enum boot98_fs_result boot98_file_write_result(
	struct boot98_file *file, uint64_t offset, const void *buffer,
	uint32_t length);
enum boot98_fs_result boot98_file_truncate_result(struct boot98_file *file,
						  uint64_t size);
enum boot98_fs_result boot98_file_flush_result(struct boot98_file *file);
int boot98_fs_readdir(struct boot98_filesystem *filesystem, const char *path,
		      unsigned index, struct boot98_dirent *entry);
enum boot98_fs_result boot98_fs_readdir_result(
	struct boot98_filesystem *filesystem, const char *path, unsigned index,
	struct boot98_dirent *entry);
enum boot98_fs_result boot98_fs_stat_result(
	struct boot98_filesystem *filesystem, const char *path,
	struct boot98_dirent *entry);
int boot98_file_contiguous_lba(struct boot98_file *file,
			       uint32_t *absolute_lba);
enum boot98_fs_result boot98_file_contiguous_lba_result(
	struct boot98_file *file, uint32_t *absolute_lba);

#endif
