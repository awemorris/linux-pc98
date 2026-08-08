/*
 * PC-98 Bootstrap Environment Noct target adapter
 * Copyright (C) 2026 Awe Morris
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef BOOT98_NOCT_PLATFORM_H
#define BOOT98_NOCT_PLATFORM_H

struct boot98_filesystem;
struct boot98_environment;
typedef int (*boot98_noct_key_fn)(void *context);
typedef int (*boot98_noct_clock_fn)(void *context);

int boot98_noct_run_embedded(unsigned repeat_count);
int boot98_noct_run_file(struct boot98_filesystem *filesystem,
			 struct boot98_environment *environment,
			 const char *path, int argc, char *const argv[],
			 boot98_noct_key_fn key_read,
			 boot98_noct_key_fn key_poll,
			 boot98_noct_clock_fn clock_second, void *key_context);
int boot98_noct_run_repl(struct boot98_filesystem *filesystem,
			 struct boot98_environment *environment,
			 boot98_noct_key_fn key_read,
			 boot98_noct_key_fn key_poll,
			 boot98_noct_clock_fn clock_second, void *key_context);

#endif
