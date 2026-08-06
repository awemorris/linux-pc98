/*
 * PC-9800 Bootloader image-loader dispatch
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "boot98-image.h"

int boot98_image_boot(const struct boot98_image_loader *loader,
		      struct boot98_filesystem *filesystem, const char *path,
		      const char *arguments)
{
	struct boot98_file file;

	if (!loader || !loader->probe || !loader->load ||
	    !boot98_fs_open(filesystem, path, &file) || !loader->probe(&file))
		return 0;
	return loader->load(&file, arguments ? arguments : "");
}
