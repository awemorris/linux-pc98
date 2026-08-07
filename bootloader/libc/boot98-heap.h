/*
 * PC-98 Bootstrap Environment freestanding C library
 * Copyright (C) 2026 Awe Morris
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef BOOT98_HEAP_H
#define BOOT98_HEAP_H

#include <stddef.h>

enum boot98_heap_event {
	BOOT98_HEAP_ALLOCATED = 0,
	BOOT98_HEAP_FREED,
};

typedef void (*boot98_heap_observer_fn)(void *context, void *pointer,
					size_t size,
					enum boot98_heap_event event);

void boot98_heap_init(void *base, size_t size);
void boot98_heap_reset(void);
void boot98_heap_set_failure_after(size_t successful_allocations);
void boot98_heap_set_observer(boot98_heap_observer_fn observer,
			      void *context);
void *boot98_malloc(size_t size);
void *boot98_calloc(size_t count, size_t size);
void *boot98_realloc(void *pointer, size_t size);
char *boot98_strdup(const char *string);
void boot98_free(void *pointer);
size_t boot98_heap_current(void);
size_t boot98_heap_peak(void);
size_t boot98_heap_error_count(void);
size_t boot98_heap_largest_free(void);
int boot98_heap_validate(void);

#endif
