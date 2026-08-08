/*
 * Boots Noct memory profile selection
 * Copyright (C) 2026 Awe Morris
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef BOOT98_NOCT_MEMORY_H
#define BOOT98_NOCT_MEMORY_H

#include <stddef.h>
#include <stdint.h>

enum boot98_noct_memory_class {
	BOOT98_NOCT_MEMORY_5,
	BOOT98_NOCT_MEMORY_8,
	BOOT98_NOCT_MEMORY_16,
	BOOT98_NOCT_MEMORY_32,
	BOOT98_NOCT_MEMORY_64,
	BOOT98_NOCT_MEMORY_LARGE,
};

struct boot98_noct_memory_profile {
	enum boot98_noct_memory_class memory_class;
	const char *name;
	uint32_t installed_mib;
	uintptr_t arena_base;
	size_t arena_size;
	size_t source_max;
	size_t repl_source_max;
	size_t gc_nursery_size;
	size_t gc_graduate_size;
	size_t gc_tenure_size;
	size_t jit_code_size;
};

int boot98_noct_select_memory(uint32_t low_extended_bytes,
			      uint32_t high_memory_mib,
			      struct boot98_noct_memory_profile *profile);

#endif
