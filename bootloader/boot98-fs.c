/*
 * PC-9800 Bootloader filesystem dispatch
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "boot98-fs.h"

static void clear_bytes(void *pointer, uint32_t length)
{
	uint8_t *bytes = pointer;

	while (length--)
		*bytes++ = 0;
}

enum boot98_fs_result boot98_volume_read_result(
	const struct boot98_volume *volume, uint32_t lba, void *buffer)
{
	if (!volume || !buffer || volume->sector_size != 512)
		return BOOT98_FS_INVALID_ARGUMENT;
	if (!volume->read)
		return BOOT98_FS_UNSUPPORTED;
	if (lba + volume->start_lba < lba)
		return BOOT98_FS_INVALID_ARGUMENT;
	return volume->read(volume->context, volume->start_lba + lba, buffer) ?
		BOOT98_FS_OK : BOOT98_FS_IO_ERROR;
}

int boot98_volume_read(const struct boot98_volume *volume, uint32_t lba,
		       void *buffer)
{
	return boot98_volume_read_result(volume, lba, buffer) == BOOT98_FS_OK;
}

enum boot98_fs_result boot98_volume_write_result(
	struct boot98_volume *volume, uint32_t lba, const void *buffer)
{
	if (!volume || !buffer || volume->sector_size != 512)
		return BOOT98_FS_INVALID_ARGUMENT;
	if (!volume->write)
		return BOOT98_FS_READ_ONLY;
	if (lba + volume->start_lba < lba)
		return BOOT98_FS_INVALID_ARGUMENT;
	return volume->write(volume->context, volume->start_lba + lba, buffer) ?
		BOOT98_FS_OK : BOOT98_FS_IO_ERROR;
}

int boot98_volume_write(struct boot98_volume *volume, uint32_t lba,
			const void *buffer)
{
	return boot98_volume_write_result(volume, lba, buffer) == BOOT98_FS_OK;
}

void boot98_fs_reset(struct boot98_filesystem *filesystem)
{
	if (filesystem)
		clear_bytes(filesystem, sizeof(*filesystem));
}

enum boot98_fs_result boot98_fs_mount_result(
	struct boot98_filesystem *filesystem, const struct boot98_volume *volume,
	const struct boot98_filesystem_driver *const *drivers,
	unsigned driver_count)
{
	enum boot98_fs_result last = BOOT98_FS_UNSUPPORTED;

	if (!filesystem || !volume || !drivers || !volume->read)
		return BOOT98_FS_INVALID_ARGUMENT;
	for (unsigned i = 0; i < driver_count; i++) {
		const struct boot98_filesystem_driver *driver = drivers[i];
		enum boot98_fs_result result;

		if (!driver || !driver->probe || !driver->mount || !driver->open ||
		    !driver->read || !driver->readdir)
			continue;
		result = driver->probe(volume);
		if (result != BOOT98_FS_OK) {
			if (result != BOOT98_FS_UNSUPPORTED ||
			    last == BOOT98_FS_UNSUPPORTED)
				last = result;
			continue;
		}
		boot98_fs_reset(filesystem);
		filesystem->driver = driver;
		filesystem->volume = *volume;
		result = driver->mount(filesystem);
		if (result == BOOT98_FS_OK)
			return result;
		last = result;
	}
	boot98_fs_reset(filesystem);
	return last;
}

int boot98_fs_mount(struct boot98_filesystem *filesystem,
		    const struct boot98_volume *volume,
		    const struct boot98_filesystem_driver *const *drivers,
		    unsigned driver_count)
{
	return boot98_fs_mount_result(filesystem, volume, drivers,
				      driver_count) == BOOT98_FS_OK;
}

enum boot98_fs_result boot98_fs_open_result(
	struct boot98_filesystem *filesystem, const char *path,
	struct boot98_file *file)
{
	enum boot98_fs_result result;

	if (!filesystem || !filesystem->driver || !path || !*path || !file)
		return BOOT98_FS_INVALID_ARGUMENT;
	clear_bytes(file, sizeof(*file));
	file->filesystem = filesystem;
	result = filesystem->driver->open(filesystem, path, file);
	if (result == BOOT98_FS_OK)
		return result;
	clear_bytes(file, sizeof(*file));
	return result;
}

int boot98_fs_open(struct boot98_filesystem *filesystem, const char *path,
		   struct boot98_file *file)
{
	return boot98_fs_open_result(filesystem, path, file) == BOOT98_FS_OK;
}

enum boot98_fs_result boot98_fs_create_result(
	struct boot98_filesystem *filesystem, const char *path,
	struct boot98_file *file)
{
	enum boot98_fs_result result;

	if (!filesystem || !filesystem->driver || !path || !*path || !file)
		return BOOT98_FS_INVALID_ARGUMENT;
	if (!filesystem->driver->create)
		return BOOT98_FS_READ_ONLY;
	clear_bytes(file, sizeof(*file));
	file->filesystem = filesystem;
	result = filesystem->driver->create(filesystem, path, file);
	if (result == BOOT98_FS_OK)
		return result;
	clear_bytes(file, sizeof(*file));
	return result;
}

enum boot98_fs_result boot98_file_read_result(
	struct boot98_file *file, uint64_t offset, void *buffer, uint32_t length,
	boot98_read_progress_t progress, void *progress_context)
{
	if (!file || !file->filesystem || !file->filesystem->driver || !buffer ||
	    offset > file->size || length > file->size - offset)
		return BOOT98_FS_INVALID_ARGUMENT;
	if (!length)
		return BOOT98_FS_OK;
	return file->filesystem->driver->read(file, offset, buffer, length,
					     progress, progress_context);
}

int boot98_file_read_progress(struct boot98_file *file, uint64_t offset,
			      void *buffer, uint32_t length,
			      boot98_read_progress_t progress,
			      void *progress_context)
{
	return boot98_file_read_result(file, offset, buffer, length, progress,
				       progress_context) == BOOT98_FS_OK;
}

int boot98_file_read(struct boot98_file *file, uint64_t offset, void *buffer,
		     uint32_t length)
{
	return boot98_file_read_progress(file, offset, buffer, length, 0, 0);
}

enum boot98_fs_result boot98_file_write_result(
	struct boot98_file *file, uint64_t offset, const void *buffer,
	uint32_t length)
{
	if (!file || !file->filesystem || !file->filesystem->driver || !buffer ||
	    (uint64_t)length > UINT64_MAX - offset)
		return BOOT98_FS_INVALID_ARGUMENT;
	if (!file->filesystem->driver->write)
		return BOOT98_FS_READ_ONLY;
	return file->filesystem->driver->write(file, offset, buffer, length);
}

enum boot98_fs_result boot98_file_truncate_result(struct boot98_file *file,
						  uint64_t size)
{
	if (!file || !file->filesystem || !file->filesystem->driver)
		return BOOT98_FS_INVALID_ARGUMENT;
	if (!file->filesystem->driver->truncate)
		return BOOT98_FS_READ_ONLY;
	return file->filesystem->driver->truncate(file, size);
}

enum boot98_fs_result boot98_file_flush_result(struct boot98_file *file)
{
	if (!file || !file->filesystem || !file->filesystem->driver)
		return BOOT98_FS_INVALID_ARGUMENT;
	if (!file->filesystem->driver->flush)
		return BOOT98_FS_READ_ONLY;
	return file->filesystem->driver->flush(file);
}

enum boot98_fs_result boot98_fs_readdir_result(
	struct boot98_filesystem *filesystem, const char *path, unsigned index,
	struct boot98_dirent *entry)
{
	if (!filesystem || !filesystem->driver || !entry)
		return BOOT98_FS_INVALID_ARGUMENT;
	clear_bytes(entry, sizeof(*entry));
	return filesystem->driver->readdir(filesystem, path ? path : "", index,
					   entry);
}

int boot98_fs_readdir(struct boot98_filesystem *filesystem, const char *path,
		      unsigned index, struct boot98_dirent *entry)
{
	return boot98_fs_readdir_result(filesystem, path, index, entry) ==
		BOOT98_FS_OK;
}

enum boot98_fs_result boot98_fs_stat_result(
	struct boot98_filesystem *filesystem, const char *path,
	struct boot98_dirent *entry)
{
	if (!filesystem || !filesystem->driver || !path || !*path || !entry)
		return BOOT98_FS_INVALID_ARGUMENT;
	if (!filesystem->driver->stat)
		return BOOT98_FS_UNSUPPORTED;
	clear_bytes(entry, sizeof(*entry));
	return filesystem->driver->stat(filesystem, path, entry);
}

enum boot98_fs_result boot98_file_contiguous_lba_result(
	struct boot98_file *file, uint32_t *absolute_lba)
{
	if (!file || !file->filesystem || !file->filesystem->driver ||
	    !absolute_lba)
		return BOOT98_FS_INVALID_ARGUMENT;
	if (!file->filesystem->driver->contiguous_lba)
		return BOOT98_FS_UNSUPPORTED;
	return file->filesystem->driver->contiguous_lba(file, absolute_lba);
}

int boot98_file_contiguous_lba(struct boot98_file *file,
			       uint32_t *absolute_lba)
{
	return boot98_file_contiguous_lba_result(file, absolute_lba) ==
		BOOT98_FS_OK;
}
