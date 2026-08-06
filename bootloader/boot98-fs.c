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

int boot98_volume_read(const struct boot98_volume *volume, uint32_t lba,
		       void *buffer)
{
	if (!volume || !volume->read || volume->sector_size != 512 ||
	    lba + volume->start_lba < lba)
		return 0;
	return volume->read(volume->context, volume->start_lba + lba, buffer);
}

void boot98_fs_reset(struct boot98_filesystem *filesystem)
{
	if (filesystem)
		clear_bytes(filesystem, sizeof(*filesystem));
}

int boot98_fs_mount(struct boot98_filesystem *filesystem,
		    const struct boot98_volume *volume,
		    const struct boot98_filesystem_driver *const *drivers,
		    unsigned driver_count)
{
	if (!filesystem || !volume || !drivers || !volume->read)
		return 0;
	for (unsigned i = 0; i < driver_count; i++) {
		const struct boot98_filesystem_driver *driver = drivers[i];

		if (!driver || !driver->probe || !driver->mount || !driver->open ||
		    !driver->read || !driver->readdir ||
		    !driver->probe(volume))
			continue;
		boot98_fs_reset(filesystem);
		filesystem->driver = driver;
		filesystem->volume = *volume;
		if (driver->mount(filesystem))
			return 1;
	}
	boot98_fs_reset(filesystem);
	return 0;
}

int boot98_fs_open(struct boot98_filesystem *filesystem, const char *path,
		   struct boot98_file *file)
{
	if (!filesystem || !filesystem->driver || !path || !*path || !file)
		return 0;
	clear_bytes(file, sizeof(*file));
	file->filesystem = filesystem;
	if (filesystem->driver->open(filesystem, path, file))
		return 1;
	clear_bytes(file, sizeof(*file));
	return 0;
}

int boot98_file_read_progress(struct boot98_file *file, uint64_t offset,
			      void *buffer, uint32_t length,
			      boot98_read_progress_t progress,
			      void *progress_context)
{
	if (!file || !file->filesystem || !file->filesystem->driver ||
	    !buffer || offset > file->size || length > file->size - offset)
		return 0;
	if (!length)
		return 1;
	return file->filesystem->driver->read(file, offset, buffer, length,
					      progress, progress_context);
}

int boot98_file_read(struct boot98_file *file, uint64_t offset, void *buffer,
		     uint32_t length)
{
	return boot98_file_read_progress(file, offset, buffer, length, 0, 0);
}

int boot98_fs_readdir(struct boot98_filesystem *filesystem, const char *path,
		      unsigned index, struct boot98_dirent *entry)
{
	if (!filesystem || !filesystem->driver || !entry)
		return 0;
	clear_bytes(entry, sizeof(*entry));
	return filesystem->driver->readdir(filesystem, path ? path : "", index,
					   entry);
}

int boot98_file_contiguous_lba(struct boot98_file *file,
			       uint32_t *absolute_lba)
{
	if (!file || !file->filesystem || !file->filesystem->driver ||
	    !file->filesystem->driver->contiguous_lba || !absolute_lba)
		return 0;
	return file->filesystem->driver->contiguous_lba(file, absolute_lba);
}
