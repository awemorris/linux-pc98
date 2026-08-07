/*
 * PC-98 Bootstrap Environment freestanding C library
 * Copyright (C) 2026 Awe Morris
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "boot98-heap.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define HEAP_ALIGNMENT 8U
#define HEAP_MAGIC 0x42393848U
#define HEAP_FREE 0x46524545U
#define HEAP_USED 0x55534544U

struct heap_block {
	uint32_t magic;
	uint32_t state;
	size_t capacity;
	size_t used;
	struct heap_block *previous_physical;
	struct heap_block *next_physical;
	struct heap_block *previous_free;
	struct heap_block *next_free;
};

static void *heap_original_base;
static size_t heap_original_size;
static uint8_t *heap_begin;
static uint8_t *heap_end;
static struct heap_block *heap_first;
static struct heap_block *heap_free_list;
static size_t heap_current_bytes;
static size_t heap_peak_bytes;
static size_t heap_errors;
static size_t heap_fail_after = SIZE_MAX;
static size_t heap_successful_allocations;

static size_t
aligned_size(size_t size)
{
	if (size > SIZE_MAX - (HEAP_ALIGNMENT - 1U))
		return 0;
	return (size + HEAP_ALIGNMENT - 1U) & ~(HEAP_ALIGNMENT - 1U);
}

static size_t
block_header_size(void)
{
	return aligned_size(sizeof(struct heap_block));
}

static uint8_t *
block_payload(struct heap_block *block)
{
	return (uint8_t *)block + block_header_size();
}

static void
remove_free(struct heap_block *block)
{
	if (block->previous_free != NULL)
		block->previous_free->next_free = block->next_free;
	else
		heap_free_list = block->next_free;
	if (block->next_free != NULL)
		block->next_free->previous_free = block->previous_free;
	block->previous_free = NULL;
	block->next_free = NULL;
}

static void
insert_free(struct heap_block *block)
{
	block->previous_free = NULL;
	block->next_free = heap_free_list;
	if (heap_free_list != NULL)
		heap_free_list->previous_free = block;
	heap_free_list = block;
}

static struct heap_block *
split_block(struct heap_block *block, size_t capacity)
{
	struct heap_block *tail;
	size_t header = block_header_size();

	if (capacity > block->capacity ||
	    block->capacity - capacity < header + HEAP_ALIGNMENT)
		return NULL;
	tail = (struct heap_block *)(block_payload(block) + capacity);
	tail->magic = HEAP_MAGIC;
	tail->state = HEAP_FREE;
	tail->capacity = block->capacity - capacity - header;
	tail->used = 0;
	tail->previous_physical = block;
	tail->next_physical = block->next_physical;
	if (tail->next_physical != NULL)
		tail->next_physical->previous_physical = tail;
	tail->previous_free = NULL;
	tail->next_free = NULL;
	block->next_physical = tail;
	block->capacity = capacity;
	insert_free(tail);
	return tail;
}

static void
merge_with_next(struct heap_block *block)
{
	struct heap_block *next = block->next_physical;

	if (next == NULL || next->state != HEAP_FREE)
		return;
	remove_free(next);
	block->capacity += block_header_size() + next->capacity;
	block->next_physical = next->next_physical;
	if (block->next_physical != NULL)
		block->next_physical->previous_physical = block;
	next->magic = 0;
}

static struct heap_block *
pointer_block(void *pointer)
{
	struct heap_block *block;
	struct heap_block *cursor;
	uint8_t *bytes = pointer;

	if (pointer == NULL || heap_begin == NULL ||
	    bytes < heap_begin + block_header_size() || bytes >= heap_end)
		return NULL;
	block = (struct heap_block *)(bytes - block_header_size());
	if ((uintptr_t)block % HEAP_ALIGNMENT != 0 ||
	    block->magic != HEAP_MAGIC || block_payload(block) != bytes)
		return NULL;
	for (cursor = heap_first; cursor != NULL;
	     cursor = cursor->next_physical)
		if (cursor == block)
			return block;
	return NULL;
}

void
boot98_heap_init(void *base, size_t size)
{
	uintptr_t raw = (uintptr_t)base;
	uintptr_t aligned;
	size_t skipped;
	size_t usable;

	heap_original_base = base;
	heap_original_size = size;
	heap_begin = NULL;
	heap_end = NULL;
	heap_first = NULL;
	heap_free_list = NULL;
	heap_current_bytes = 0;
	heap_peak_bytes = 0;
	heap_errors = 0;
	heap_successful_allocations = 0;
	heap_fail_after = SIZE_MAX;
	if (base == NULL || raw > UINTPTR_MAX - (HEAP_ALIGNMENT - 1U))
		return;
	aligned = (raw + HEAP_ALIGNMENT - 1U) &
		~(uintptr_t)(HEAP_ALIGNMENT - 1U);
	skipped = (size_t)(aligned - raw);
	if (skipped > size)
		return;
	usable = (size - skipped) & ~(HEAP_ALIGNMENT - 1U);
	if (usable < block_header_size() + HEAP_ALIGNMENT ||
	    usable > UINTPTR_MAX - aligned)
		return;
	heap_begin = (uint8_t *)aligned;
	heap_end = heap_begin + usable;
	heap_first = (struct heap_block *)heap_begin;
	heap_first->magic = HEAP_MAGIC;
	heap_first->state = HEAP_FREE;
	heap_first->capacity = usable - block_header_size();
	heap_first->used = 0;
	heap_first->previous_physical = NULL;
	heap_first->next_physical = NULL;
	heap_first->previous_free = NULL;
	heap_first->next_free = NULL;
	heap_free_list = heap_first;
}

void
boot98_heap_reset(void)
{
	size_t failure = heap_fail_after;
	boot98_heap_init(heap_original_base, heap_original_size);
	heap_fail_after = failure;
}

void
boot98_heap_set_failure_after(size_t successful_allocations)
{
	heap_fail_after = successful_allocations;
	heap_successful_allocations = 0;
}

void *
boot98_malloc(size_t size)
{
	struct heap_block *block;
	size_t requested = size;
	size_t capacity;

	if (size == 0)
		size = 1;
	capacity = aligned_size(size);
	if (capacity == 0 || heap_successful_allocations >= heap_fail_after)
		return NULL;
	for (block = heap_free_list; block != NULL; block = block->next_free) {
		if (block->capacity >= capacity)
			break;
	}
	if (block == NULL)
		return NULL;
	remove_free(block);
	(void)split_block(block, capacity);
	block->state = HEAP_USED;
	block->used = requested;
	heap_current_bytes += requested;
	if (heap_current_bytes > heap_peak_bytes)
		heap_peak_bytes = heap_current_bytes;
	heap_successful_allocations++;
	return block_payload(block);
}

void *
boot98_calloc(size_t count, size_t size)
{
	void *pointer;
	size_t total;

	if (count != 0 && size > SIZE_MAX / count)
		return NULL;
	total = count * size;
	pointer = boot98_malloc(total);
	if (pointer != NULL)
		memset(pointer, 0, total);
	return pointer;
}

void
boot98_free(void *pointer)
{
	struct heap_block *block;
	struct heap_block *previous;

	if (pointer == NULL)
		return;
	block = pointer_block(pointer);
	if (block == NULL || block->state != HEAP_USED) {
		heap_errors++;
		return;
	}
	heap_current_bytes -= block->used;
	block->used = 0;
	block->state = HEAP_FREE;
	merge_with_next(block);
	previous = block->previous_physical;
	if (previous != NULL && previous->state == HEAP_FREE) {
		remove_free(previous);
		previous->capacity += block_header_size() + block->capacity;
		previous->next_physical = block->next_physical;
		if (previous->next_physical != NULL)
			previous->next_physical->previous_physical = previous;
		block->magic = 0;
		block = previous;
	}
	insert_free(block);
}

void *
boot98_realloc(void *pointer, size_t size)
{
	struct heap_block *block;
	struct heap_block *tail;
	void *replacement;
	size_t capacity;
	size_t old_used;

	if (pointer == NULL)
		return boot98_malloc(size);
	if (size == 0) {
		boot98_free(pointer);
		return NULL;
	}
	block = pointer_block(pointer);
	if (block == NULL || block->state != HEAP_USED) {
		heap_errors++;
		return NULL;
	}
	capacity = aligned_size(size);
	if (capacity == 0)
		return NULL;
	old_used = block->used;
	if (capacity <= block->capacity) {
		tail = split_block(block, capacity);
		if (tail != NULL)
			merge_with_next(tail);
		block->used = size;
		heap_current_bytes = heap_current_bytes - old_used + size;
		if (heap_current_bytes > heap_peak_bytes)
			heap_peak_bytes = heap_current_bytes;
		return pointer;
	}
	if (block->next_physical != NULL &&
	    block->next_physical->state == HEAP_FREE &&
	    block->capacity + block_header_size() +
	    block->next_physical->capacity >= capacity) {
		merge_with_next(block);
		(void)split_block(block, capacity);
		block->used = size;
		heap_current_bytes = heap_current_bytes - old_used + size;
		if (heap_current_bytes > heap_peak_bytes)
			heap_peak_bytes = heap_current_bytes;
		return pointer;
	}
	replacement = boot98_malloc(size);
	if (replacement == NULL)
		return NULL;
	memcpy(replacement, pointer, old_used < size ? old_used : size);
	boot98_free(pointer);
	return replacement;
}

char *
boot98_strdup(const char *string)
{
	size_t length;
	char *copy;

	if (string == NULL)
		return NULL;
	length = strlen(string);
	if (length == SIZE_MAX)
		return NULL;
	copy = boot98_malloc(length + 1U);
	if (copy != NULL)
		memcpy(copy, string, length + 1U);
	return copy;
}

size_t
boot98_heap_current(void)
{
	return heap_current_bytes;
}

size_t
boot98_heap_peak(void)
{
	return heap_peak_bytes;
}

size_t
boot98_heap_error_count(void)
{
	return heap_errors;
}

size_t
boot98_heap_largest_free(void)
{
	struct heap_block *block;
	size_t largest = 0;

	for (block = heap_free_list; block != NULL; block = block->next_free) {
		if (block->capacity > largest)
			largest = block->capacity;
	}
	return largest;
}

int
boot98_heap_validate(void)
{
	struct heap_block *block;
	struct heap_block *previous = NULL;
	struct heap_block *free_block;
	struct heap_block *previous_free = NULL;
	struct heap_block *slow;
	struct heap_block *fast;
	size_t used = 0;
	size_t span = 0;
	size_t physical_free_count = 0;
	size_t list_free_count = 0;

	if (heap_first == NULL)
		return heap_begin == NULL && heap_free_list == NULL;
	for (block = heap_first; block != NULL; block = block->next_physical) {
		if (block->magic != HEAP_MAGIC ||
		    block->previous_physical != previous ||
		    (uint8_t *)block != heap_begin + span ||
		    (block->state != HEAP_FREE && block->state != HEAP_USED))
			return 0;
		if (block->capacity > (size_t)(heap_end - (uint8_t *)block) ||
		    block_header_size() > (size_t)(heap_end - (uint8_t *)block) ||
		    block->capacity >
		    (size_t)(heap_end - (uint8_t *)block) - block_header_size())
			return 0;
		span += block_header_size() + block->capacity;
		if (block->state == HEAP_USED) {
			if (block->used > block->capacity)
				return 0;
			used += block->used;
		} else {
			physical_free_count++;
			if (block->next_physical != NULL &&
			    block->next_physical->state == HEAP_FREE)
				return 0;
		}
		previous = block;
	}
	slow = heap_free_list;
	fast = heap_free_list;
	while (fast != NULL && fast->next_free != NULL) {
		slow = slow->next_free;
		fast = fast->next_free->next_free;
		if (slow == fast)
			return 0;
	}
	for (free_block = heap_free_list; free_block != NULL;
	     free_block = free_block->next_free) {
		int found = 0;
		if (free_block->magic != HEAP_MAGIC ||
		    free_block->state != HEAP_FREE ||
		    free_block->previous_free != previous_free)
			return 0;
		for (block = heap_first; block != NULL;
		     block = block->next_physical)
			if (block == free_block)
				found++;
		if (found != 1)
			return 0;
		previous_free = free_block;
		list_free_count++;
		if (list_free_count > physical_free_count)
			return 0;
	}
	return heap_begin + span == heap_end && used == heap_current_bytes &&
		list_free_count == physical_free_count;
}

void *
noct_pc98be_malloc(size_t size)
{
	return boot98_malloc(size);
}

void *
noct_pc98be_calloc(size_t count, size_t size)
{
	return boot98_calloc(count, size);
}

void *
noct_pc98be_realloc(void *pointer, size_t size)
{
	return boot98_realloc(pointer, size);
}

char *
noct_pc98be_strdup(const char *string)
{
	return boot98_strdup(string);
}

void
noct_pc98be_free(void *pointer)
{
	boot98_free(pointer);
}

/* Standard names are real symbols, not preprocessor aliases.  Flex-generated
 * Noct sources intentionally redefine malloc/realloc/free locally for AST
 * allocation, so global macros would make that otherwise valid code noisy. */
void *malloc(size_t size) { return boot98_malloc(size); }
void *calloc(size_t count, size_t size) { return boot98_calloc(count, size); }
void *realloc(void *pointer, size_t size)
{
	return boot98_realloc(pointer, size);
}
void free(void *pointer) { boot98_free(pointer); }
