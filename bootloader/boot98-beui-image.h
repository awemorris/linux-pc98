/*
 * Boots BeUI image decoders
 * Copyright (C) 2026 Awe Morris
 * SPDX-License-Identifier: Zlib
 */

#ifndef BOOT98_BEUI_IMAGE_H
#define BOOT98_BEUI_IMAGE_H

#include "boot98-beui.h"

#include <stddef.h>

/* Only uncompressed Windows BMP with 1, 4, 8, or 24 bits per pixel. */
int boot98_beui_bmp_measure(const void *data, size_t size,
			     enum boot98_beui_image_format *format,
			     unsigned *width, unsigned *height,
			     size_t *pixel_bytes);
int boot98_beui_bmp_decode(const void *data, size_t size, void *pixel_storage,
			    size_t pixel_capacity,
			    struct boot98_beui_image *image);

#endif
