/*
 * PC-98 Bootstrap Environment Noct lifecycle
 * Copyright (C) 2026 Awe Morris
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef BOOT98_NOCT_H
#define BOOT98_NOCT_H

#include <stddef.h>

#define BOOT98_NOCT_NO_FAILURE ((size_t)-1)

enum boot98_noct_status {
	BOOT98_NOCT_OK = 0,
	BOOT98_NOCT_INVALID_ARGUMENT,
	BOOT98_NOCT_BUSY,
	BOOT98_NOCT_VM_ERROR,
	BOOT98_NOCT_API_ERROR,
	BOOT98_NOCT_SOURCE_ERROR,
	BOOT98_NOCT_RUNTIME_ERROR,
	BOOT98_NOCT_CLEANUP_ERROR,
};

typedef size_t (*boot98_noct_write_fn)(void *context, const char *bytes,
				       size_t length);

struct boot98_noct_options {
	void *arena;
	size_t arena_size;
	size_t fail_after;
	boot98_noct_write_fn write;
	void *write_context;
};

struct boot98_noct_result {
	enum boot98_noct_status status;
	size_t heap_peak;
	size_t bytes_before_reset;
	size_t current_after_reset;
	size_t heap_errors;
};

int boot98_noct_run(const char *source_name, const char *source,
		    const struct boot98_noct_options *options,
		    struct boot98_noct_result *result);
const char *boot98_noct_status_string(enum boot98_noct_status status);

#endif
