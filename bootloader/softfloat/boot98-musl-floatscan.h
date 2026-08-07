/*
 * String-only FILE compatibility for musl floatscan.
 * Copyright (C) 2026 Awe Morris
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef BOOT98_MUSL_FLOATSCAN_H
#define BOOT98_MUSL_FLOATSCAN_H

#include <stddef.h>
#include <sys/types.h>

#define BOOT98_STDIO_H
#define _STDIO_IMPL_H
#define EOF (-1)
#define hidden __attribute__((__visibility__("hidden")))

typedef struct boot98_musl_scan_file {
	unsigned char *rpos;
	unsigned char *rend;
	unsigned char *buf;
	unsigned char *shend;
	off_t shlim;
	off_t shcnt;
} FILE;

int __uflow(FILE *stream);

#endif
