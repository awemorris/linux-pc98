/* Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef BOOT98_UNISTD_H
#define BOOT98_UNISTD_H

#include <stddef.h>

#define F_OK 0

int access(const char *path, int mode);
char *getcwd(char *buffer, size_t size);
int chdir(const char *path);
int isatty(int descriptor);
int fileno(void *stream);

#endif
