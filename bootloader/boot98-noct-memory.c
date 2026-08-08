/*
 * Boots Noct memory profile selection
 * Copyright (C) 2026 Awe Morris
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "boot98-noct-memory.h"

#define KIB (1024U)
#define MIB (1024U * KIB)
#define LOW_ARENA_BASE (1U * MIB)
#define HIGH_ARENA_BASE (16U * MIB)
#define LOW_EXTENDED_MAX (14U * MIB)
#define ARENA_GUARD (64U * KIB)

struct profile_limits {
	const char *name;
	size_t arena_cap;
	size_t source_max;
	size_t repl_source_max;
	size_t nursery;
	size_t graduate;
	size_t tenure;
	size_t jit;
};

static const struct profile_limits limits[] = {
	[BOOT98_NOCT_MEMORY_5] = {
		"5M", 4U * MIB, 256U * KIB, 8U * KIB,
		128U * KIB, 32U * KIB, 512U * KIB, 96U * KIB,
	},
	[BOOT98_NOCT_MEMORY_8] = {
		"8M", 8U * MIB, 256U * KIB, 16U * KIB,
		256U * KIB, 64U * KIB, 1U * MIB, 192U * KIB,
	},
	[BOOT98_NOCT_MEMORY_16] = {
		"16M", 14U * MIB, 256U * KIB, 32U * KIB,
		512U * KIB, 128U * KIB, 2U * MIB, 256U * KIB,
	},
	[BOOT98_NOCT_MEMORY_32] = {
		"32M", 16U * MIB, 256U * KIB, 32U * KIB,
		1U * MIB, 256U * KIB, 4U * MIB, 512U * KIB,
	},
	[BOOT98_NOCT_MEMORY_64] = {
		"64M", 48U * MIB, 256U * KIB, 32U * KIB,
		2U * MIB, 256U * KIB, 8U * MIB, 1U * MIB,
	},
	[BOOT98_NOCT_MEMORY_LARGE] = {
		">64M", 64U * MIB, 256U * KIB, 32U * KIB,
		2U * MIB, 512U * KIB, 16U * MIB, 2U * MIB,
	},
};

static enum boot98_noct_memory_class
classify(uint32_t installed_mib)
{
	if (installed_mib < 8U)
		return BOOT98_NOCT_MEMORY_5;
	if (installed_mib < 16U)
		return BOOT98_NOCT_MEMORY_8;
	if (installed_mib < 32U)
		return BOOT98_NOCT_MEMORY_16;
	if (installed_mib < 64U)
		return BOOT98_NOCT_MEMORY_32;
	if (installed_mib == 64U)
		return BOOT98_NOCT_MEMORY_64;
	return BOOT98_NOCT_MEMORY_LARGE;
}

int
boot98_noct_select_memory(uint32_t low_extended_bytes,
			  uint32_t high_memory_mib,
			  struct boot98_noct_memory_profile *profile)
{
	const struct profile_limits *selected;
	uint64_t high_bytes;
	uint32_t installed_mib;
	uintptr_t arena_base;
	size_t available;
	size_t arena_size;
	enum boot98_noct_memory_class memory_class;

	if (profile == NULL)
		return 0;
	if (low_extended_bytes > LOW_EXTENDED_MAX)
		low_extended_bytes = LOW_EXTENDED_MAX;
	high_bytes = (uint64_t)high_memory_mib * MIB;
	if (high_memory_mib != 0U) {
		installed_mib = high_memory_mib > UINT32_MAX - 16U ?
			UINT32_MAX : high_memory_mib + 16U;
		if (high_bytes > low_extended_bytes) {
			arena_base = HIGH_ARENA_BASE;
			available = high_bytes > SIZE_MAX ?
				SIZE_MAX : (size_t)high_bytes;
		} else {
			arena_base = LOW_ARENA_BASE;
			available = low_extended_bytes;
		}
	} else {
		/* A full 14 MiB below the PC-98 hole represents a 16 MiB system. */
		installed_mib = low_extended_bytes >= LOW_EXTENDED_MAX ?
			16U : 1U + low_extended_bytes / MIB;
		arena_base = LOW_ARENA_BASE;
		available = low_extended_bytes;
	}
	if (available <= ARENA_GUARD)
		return 0;
	available -= ARENA_GUARD;
	memory_class = classify(installed_mib);
	selected = &limits[memory_class];
	arena_size = available < selected->arena_cap ?
		available : selected->arena_cap;
	if (arena_size < 2U * MIB)
		return 0;

	profile->memory_class = memory_class;
	profile->name = selected->name;
	profile->installed_mib = installed_mib;
	profile->arena_base = arena_base;
	profile->arena_size = arena_size;
	profile->source_max = selected->source_max;
	profile->repl_source_max = selected->repl_source_max;
	profile->gc_nursery_size = selected->nursery;
	profile->gc_graduate_size = selected->graduate;
	profile->gc_tenure_size = selected->tenure;
	profile->jit_code_size = selected->jit;
	return 1;
}
