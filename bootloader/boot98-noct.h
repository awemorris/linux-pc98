/*
 * PC-98 Bootstrap Environment Noct lifecycle
 * Copyright (C) 2026 Awe Morris
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef BOOT98_NOCT_H
#define BOOT98_NOCT_H

#include <stddef.h>
#include <stdint.h>

struct boot98_filesystem;

#define BOOT98_NOCT_NO_FAILURE ((size_t)-1)
#define BOOT98_NOCT_REPL_LINE_MAX 256U
#define BOOT98_NOCT_REPL_SOURCE_MAX (32U * 1024U)
#ifndef BOOT98_NOCT_JIT_CODE_MAX
#define BOOT98_NOCT_JIT_CODE_MAX (192U * 1024U)
#endif

enum boot98_noct_status {
	BOOT98_NOCT_OK = 0,
	BOOT98_NOCT_INVALID_ARGUMENT,
	BOOT98_NOCT_BUSY,
	BOOT98_NOCT_VM_ERROR,
	BOOT98_NOCT_API_ERROR,
	BOOT98_NOCT_SOURCE_ERROR,
	BOOT98_NOCT_SIGNATURE_ERROR,
	BOOT98_NOCT_RUNTIME_ERROR,
	BOOT98_NOCT_INPUT_ERROR,
	BOOT98_NOCT_CLEANUP_ERROR,
};

typedef size_t (*boot98_noct_write_fn)(void *context, const char *bytes,
				       size_t length);
typedef void (*boot98_noct_jit_code_fn)(void *context, const void *code,
					size_t length);

enum boot98_noct_repl_input_result {
	BOOT98_NOCT_REPL_INPUT_ERROR = -1,
	BOOT98_NOCT_REPL_INPUT_EXIT = 0,
	BOOT98_NOCT_REPL_INPUT_LINE = 1,
};

typedef enum boot98_noct_repl_input_result
(*boot98_noct_repl_read_fn)(void *context, int continuation, char *line,
			    size_t capacity);

struct boot98_noct_options {
	void *arena;
	size_t arena_size;
	size_t fail_after;
	int jit_enable;
	int jit_threshold;
	boot98_noct_write_fn write;
	void *write_context;
	boot98_noct_jit_code_fn observe_jit_code;
	void *jit_context;
	const struct boot98_noct_services *services;
	struct boot98_filesystem *filesystem;
};

struct boot98_noct_result {
	enum boot98_noct_status status;
	size_t heap_peak;
	size_t bytes_before_reset;
	size_t current_after_reset;
	size_t heap_errors;
	size_t jit_code_size;
	int jit_region_released;
	int64_t script_status;
};

int boot98_noct_run_args(const char *source_name, const char *source,
			 int argc, char *const argv[],
			 const struct boot98_noct_options *options,
			 struct boot98_noct_result *result);
int boot98_noct_run(const char *source_name, const char *source,
		    const struct boot98_noct_options *options,
		    struct boot98_noct_result *result);
int boot98_noct_repl(const struct boot98_noct_options *options,
		     boot98_noct_repl_read_fn read_line, void *read_context,
		     struct boot98_noct_result *result);
const char *boot98_noct_status_string(enum boot98_noct_status status);

#endif
