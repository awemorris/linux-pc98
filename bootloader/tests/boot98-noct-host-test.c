/*
 * PC-98 Bootstrap Environment Noct M6 JIT lifecycle test
 * Copyright (C) 2026 Awe Morris
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "boot98-noct.h"
#include "boot98-noct-m6-script.h"
#include "boot98-heap.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define ARENA_SIZE (6U * 1024U * 1024U)
#define OUTPUT_SIZE 1024U
#define SYS_WRITE 4
#define SYS_OPEN 5
#define SYS_CLOSE 6
#define SYS_MPROTECT 125
#define OPEN_FLAGS 01101
#define OPEN_MODE 0644

static unsigned char arena[ARENA_SIZE] __attribute__((aligned(4096)));
static unsigned char jit_capture[BOOT98_NOCT_JIT_CODE_MAX];
static size_t jit_capture_length;
static char output[OUTPUT_SIZE];
static size_t output_length;

static long
host_syscall3(long number, long argument1, long argument2, long argument3)
{
	long result;

	__asm__ volatile ("int $0x80" : "=a"(result) : "0"(number),
			  "b"(argument1), "c"(argument2), "d"(argument3) :
			  "memory");
	return result;
}

static int
write_capture_file(const char *path)
{
	long descriptor;
	size_t offset = 0;

	descriptor = host_syscall3(SYS_OPEN, (long)(uintptr_t)path,
				   OPEN_FLAGS, OPEN_MODE);
	if (descriptor < 0)
		return 0;
	while (offset < jit_capture_length) {
		long count = host_syscall3(SYS_WRITE, descriptor,
					   (long)(uintptr_t)(jit_capture + offset),
					   (long)(jit_capture_length - offset));
		if (count <= 0) {
			(void)host_syscall3(SYS_CLOSE, descriptor, 0, 0);
			return 0;
		}
		offset += (size_t)count;
	}
	return host_syscall3(SYS_CLOSE, descriptor, 0, 0) == 0;
}

static size_t
capture_output(void *context, const char *bytes, size_t length)
{
	size_t available;

	(void)context;
	available = OUTPUT_SIZE - 1U - output_length;
	if (length > available)
		length = available;
	memcpy(output + output_length, bytes, length);
	output_length += length;
	output[output_length] = '\0';
	return length;
}

static void
capture_jit(void *context, const void *code, size_t length)
{
	(void)context;
	if (length > sizeof(jit_capture)) {
		jit_capture_length = 0;
		return;
	}
	memcpy(jit_capture, code, length);
	jit_capture_length = length;
}

static int
run_case(const char *source, int jit_enable,
	 enum boot98_noct_status expected, const char *expected_output,
	 struct boot98_noct_result *returned_result)
{
	struct boot98_noct_options options;
	struct boot98_noct_result result;
	int success;

	output_length = 0;
	output[0] = '\0';
	options.arena = arena;
	options.arena_size = sizeof(arena);
	options.fail_after = BOOT98_NOCT_NO_FAILURE;
	options.jit_enable = jit_enable;
	options.jit_threshold = 1;
	options.write = capture_output;
	options.write_context = NULL;
	options.observe_jit_code = capture_jit;
	options.jit_context = NULL;
	success = boot98_noct_run("m6-test.nct", source, &options, &result);
	if (success != (expected == BOOT98_NOCT_OK) ||
	    result.status != expected)
		return 10 + (int)result.status;
	if (result.current_after_reset != 0 || boot98_heap_current() != 0 ||
	    result.heap_errors != 0 || !boot98_heap_validate())
		return 20;
	if (expected_output != NULL && strcmp(output, expected_output) != 0)
		return 30;
	if (expected_output == NULL && output_length == 0)
		return 31;
	if (jit_enable) {
		if (result.jit_code_size != BOOT98_NOCT_JIT_CODE_MAX)
			return 32;
		if (!result.jit_region_released)
			return 34;
		if (jit_capture_length != BOOT98_NOCT_JIT_CODE_MAX)
			return 35;
	} else if (result.jit_code_size != 0 || result.jit_region_released) {
		return 33;
	}
	if (returned_result != NULL)
		*returned_result = result;
	return 0;
}

int
main(int argc, char **argv)
{
	static const char syntax_error[] = "func main( {";
	static const char runtime_error[] =
		"func main() { Console.write(1); }";
	static char interpreter_output[OUTPUT_SIZE];
	struct boot98_noct_result result;
	unsigned iteration;
	int status;

	if (argc != 2 ||
	    host_syscall3(SYS_MPROTECT, (long)(uintptr_t)arena,
			  sizeof(arena), 7) != 0)
		return 1;
	status = run_case(BOOT98_NOCT_M6_SOURCE, 0, BOOT98_NOCT_OK,
			  BOOT98_NOCT_M6_OUTPUT, &result);
	if (status != 0)
		return status;
	memcpy(interpreter_output, output, output_length + 1U);
	for (iteration = 0; iteration < 100U; iteration++) {
		status = run_case(BOOT98_NOCT_M6_SOURCE, 1, BOOT98_NOCT_OK,
				  BOOT98_NOCT_M6_OUTPUT, &result);
		if (status != 0 || strcmp(output, interpreter_output) != 0)
			return status != 0 ? 40 + status : 79;
	}
	status = run_case(syntax_error, 0, BOOT98_NOCT_SOURCE_ERROR, NULL,
			  &result);
	if (status != 0)
		return 80 + status;
	status = run_case(runtime_error, 0, BOOT98_NOCT_RUNTIME_ERROR, NULL,
			  &result);
	if (status != 0)
		return 120 + status;
	if (!write_capture_file(argv[1]))
		return 200;
	return 0;
}
