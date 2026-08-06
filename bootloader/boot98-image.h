/*
 * PC-9800 Bootloader image-loader interface
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef BOOT98_IMAGE_H
#define BOOT98_IMAGE_H

#include "boot98-fs.h"

struct boot98_image_loader {
	const char *name;
	int (*probe)(struct boot98_file *file);
	int (*load)(struct boot98_file *file, const char *arguments);
};

int boot98_image_boot(const struct boot98_image_loader *loader,
		      struct boot98_filesystem *filesystem, const char *path,
		      const char *arguments);

#endif
