/* Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef BOOT98_STDIO_FS_H
#define BOOT98_STDIO_FS_H

struct boot98_filesystem;
struct boot98_environment;

void boot98_stdio_set_filesystem(struct boot98_filesystem *filesystem);
void boot98_stdio_set_environment(struct boot98_environment *environment);
int boot98_stdio_close_all(void);

#endif
