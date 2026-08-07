/*
 * PC-98 Bootstrap Environment Noct lifecycle
 * Copyright (C) 2026 Awe Morris
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "boot98-noct.h"
#include "libc/boot98-heap.h"

#include <noct/noct.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

struct active_console {
	boot98_noct_write_fn write;
	void *context;
};

struct jit_observation {
	void *region;
	size_t size;
	int released;
};

static struct active_console active_console;
static int lifecycle_active;

static void
observe_heap(void *context, void *pointer, size_t size,
	     enum boot98_heap_event event)
{
	struct jit_observation *jit = context;

	if (event == BOOT98_HEAP_ALLOCATED &&
	    size == BOOT98_NOCT_JIT_CODE_MAX && jit->region == NULL) {
		jit->region = pointer;
		jit->size = size;
	} else if (event == BOOT98_HEAP_FREED && pointer == jit->region) {
		jit->released = 1;
	}
}

static void
emit_bytes(const char *bytes, size_t length)
{
	if (active_console.write != NULL && length != 0)
		(void)active_console.write(active_console.context, bytes, length);
}

static void
emit_string(const char *string)
{
	if (string != NULL)
		emit_bytes(string, strlen(string));
}

static void
emit_noct_error(NoctEnv *env, const char *kind)
{
	const char *file = "<unknown>";
	const char *message = "Noct error";
	char line_buffer[24];
	int line = 0;

	if (env != NULL) {
		(void)noct_get_error_file(env, &file);
		(void)noct_get_error_line(env, &line);
		(void)noct_get_error_message(env, &message);
	}
	emit_string(kind);
	emit_string(": ");
	emit_string(file != NULL ? file : "<unknown>");
	(void)snprintf(line_buffer, sizeof(line_buffer), ":%d: ", line);
	emit_string(line_buffer);
	emit_string(message != NULL ? message : "Noct error");
	emit_string("\n");
}

static bool
cfunc_console_write(NoctEnv *env)
{
	NoctValue value;
	const char *text;

	memset(&value, 0, sizeof(value));
	if (!noct_pin_local(env, 1, &value))
		return false;
	if (!noct_get_arg(env, 0, &value) ||
	    !noct_get_string(env, &value, &text)) {
		(void)noct_unpin_local(env, 1, &value);
		return false;
	}
	emit_bytes(text, strlen(text));
	(void)noct_unpin_local(env, 1, &value);
	return true;
}

static bool
register_console(NoctEnv *env)
{
	static const char *parameters[] = { "text" };
	NoctValue dictionary;
	NoctValue function;

	memset(&dictionary, 0, sizeof(dictionary));
	memset(&function, 0, sizeof(function));
	if (!noct_make_empty_dict(env, &dictionary) ||
	    !noct_set_global(env, "Console", &dictionary) ||
	    !noct_register_cfunc(env, "Console.write", 1, parameters,
				 cfunc_console_write, NULL) ||
	    !noct_get_global(env, "Console.write", &function) ||
	    !noct_set_dict_elem_cstr(env, &dictionary, "write", &function))
		return false;
	return true;
}

int
boot98_noct_run(const char *source_name, const char *source,
		const struct boot98_noct_options *options,
		struct boot98_noct_result *result)
{
	NoctConfig config;
	NoctVM *vm = NULL;
	NoctEnv *env = NULL;
	NoctValue return_value;
	struct jit_observation jit;
	enum boot98_noct_status status = BOOT98_NOCT_INVALID_ARGUMENT;
	int vm_created = 0;
	size_t peak = 0;
	size_t before_reset = 0;
	size_t after_reset = 0;
	size_t errors = 0;

	memset(&return_value, 0, sizeof(return_value));
	memset(&jit, 0, sizeof(jit));
	if (result != NULL)
		memset(result, 0, sizeof(*result));
	if (source_name == NULL || source == NULL || options == NULL ||
	    options->arena == NULL || options->arena_size < 4096U ||
	    options->write == NULL || options->jit_threshold < 0)
		goto finish_without_heap;
	if (lifecycle_active) {
		status = BOOT98_NOCT_BUSY;
		goto finish_without_heap;
	}

	lifecycle_active = 1;
	active_console.write = options->write;
	active_console.context = options->write_context;
	boot98_heap_init(options->arena, options->arena_size);
	boot98_heap_set_observer(observe_heap, &jit);
	if (options->fail_after != BOOT98_NOCT_NO_FAILURE)
		boot98_heap_set_failure_after(options->fail_after);

	noct_set_default_config(&config);
	config.jit_enable = options->jit_enable != 0;
	config.jit_threshold = options->jit_threshold;
	if (!noct_create_vm(&vm, &env, &config)) {
		status = BOOT98_NOCT_VM_ERROR;
		emit_string("Noct: unable to create VM\n");
		goto cleanup;
	}
	vm_created = 1;
	if (!register_console(env)) {
		status = BOOT98_NOCT_API_ERROR;
		emit_noct_error(env, "Noct API error");
		goto cleanup;
	}
	if (!noct_register_source(env, source_name, source)) {
		status = BOOT98_NOCT_SOURCE_ERROR;
		emit_noct_error(env, "Noct source error");
		goto cleanup;
	}
	if (!noct_enter_vm(env, "main", 0, NULL, &return_value)) {
		status = BOOT98_NOCT_RUNTIME_ERROR;
		emit_noct_error(env, "Noct runtime error");
		goto cleanup;
	}
	status = BOOT98_NOCT_OK;

cleanup:
	if (jit.region != NULL && options->observe_jit_code != NULL)
		options->observe_jit_code(options->jit_context, jit.region,
					  jit.size);
	if (vm_created && !noct_destroy_vm(vm) && status == BOOT98_NOCT_OK)
		status = BOOT98_NOCT_CLEANUP_ERROR;
	peak = boot98_heap_peak();
	before_reset = boot98_heap_current();
	errors = boot98_heap_error_count();
	boot98_heap_set_observer(NULL, NULL);
	boot98_heap_reset();
	after_reset = boot98_heap_current();
	if ((after_reset != 0 || errors != 0) && status == BOOT98_NOCT_OK)
		status = BOOT98_NOCT_CLEANUP_ERROR;
	active_console.write = NULL;
	active_console.context = NULL;
	lifecycle_active = 0;

finish_without_heap:
	if (result != NULL) {
		result->status = status;
		result->heap_peak = peak;
		result->bytes_before_reset = before_reset;
		result->current_after_reset = after_reset;
		result->heap_errors = errors;
		result->jit_code_size = jit.size;
		result->jit_region_released = jit.released;
	}
	return status == BOOT98_NOCT_OK;
}

const char *
boot98_noct_status_string(enum boot98_noct_status status)
{
	switch (status) {
	case BOOT98_NOCT_OK:
		return "ok";
	case BOOT98_NOCT_INVALID_ARGUMENT:
		return "invalid argument";
	case BOOT98_NOCT_BUSY:
		return "lifecycle busy";
	case BOOT98_NOCT_VM_ERROR:
		return "VM creation failed";
	case BOOT98_NOCT_API_ERROR:
		return "API registration failed";
	case BOOT98_NOCT_SOURCE_ERROR:
		return "source error";
	case BOOT98_NOCT_RUNTIME_ERROR:
		return "runtime error";
	case BOOT98_NOCT_CLEANUP_ERROR:
		return "cleanup error";
	default:
		return "unknown";
	}
}
