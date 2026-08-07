/*
 * PC-98 Bootstrap Environment Noct M4 lifecycle test
 * Copyright (C) 2026 Awe Morris
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "boot98-noct.h"
#include "boot98-heap.h"

#include <stddef.h>
#include <string.h>

#define ARENA_SIZE (6U * 1024U * 1024U)
#define OUTPUT_SIZE 1024U

static unsigned char arena[ARENA_SIZE];
static char output[OUTPUT_SIZE];
static size_t output_length;

__attribute__((noreturn)) void
boot98_noct_float_unavailable(void)
{
	for (;;)
		__asm__ volatile ("hlt");
}

static size_t
capture(void *context, const char *bytes, size_t length)
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

static int
run_case(const char *source, enum boot98_noct_status expected,
	 int expect_output)
{
	struct boot98_noct_options options;
	struct boot98_noct_result result;
	int success;

	output_length = 0;
	output[0] = '\0';
	options.arena = arena;
	options.arena_size = sizeof(arena);
	options.fail_after = BOOT98_NOCT_NO_FAILURE;
	options.write = capture;
	options.write_context = NULL;
	success = boot98_noct_run("m4-test.nct", source, &options, &result);
	if (success != (expected == BOOT98_NOCT_OK) ||
	    result.status != expected)
		return 10 + (int)result.status;
	if (result.current_after_reset != 0 || boot98_heap_current() != 0 ||
	    result.heap_errors != 0 || !boot98_heap_validate())
		return 20;
	if (expect_output && strcmp(output, "M4 ok") != 0)
		return 30;
	if (!expect_output && output_length == 0)
		return 31;
	return 0;
}

int
main(void)
{
	static const char good[] =
		"func main() { Console.write(\"M4 ok\"); }";
	static const char syntax_error[] = "func main( {";
	static const char runtime_error[] =
		"func main() { Console.write(1); }";
	unsigned iteration;
	int result;

	for (iteration = 0; iteration < 100U; iteration++) {
		result = run_case(good, BOOT98_NOCT_OK, 1);
		if (result != 0)
			return result;
	}
	result = run_case(syntax_error, BOOT98_NOCT_SOURCE_ERROR, 0);
	if (result != 0)
		return 40 + result;
	result = run_case(runtime_error, BOOT98_NOCT_RUNTIME_ERROR, 0);
	if (result != 0)
		return 80 + result;
	return 0;
}
