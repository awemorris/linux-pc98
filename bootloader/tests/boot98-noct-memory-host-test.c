/*
 * Boots Noct memory profile host tests
 * Copyright (C) 2026 Awe Morris
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "boot98-noct-memory.h"

#include <stdint.h>

#define KIB 1024U
#define MIB (1024U * KIB)

#define CHECK(expression) do { if (!(expression)) return __LINE__; } while (0)

static int
check_profile(uint32_t low, uint32_t high,
	      enum boot98_noct_memory_class expected_class,
	      uint32_t expected_installed, uintptr_t expected_base,
	      size_t expected_arena)
{
	struct boot98_noct_memory_profile profile;

	CHECK(boot98_noct_select_memory(low, high, &profile));
	CHECK(profile.memory_class == expected_class);
	CHECK(profile.installed_mib == expected_installed);
	CHECK(profile.arena_base == expected_base);
	CHECK(profile.arena_size == expected_arena);
	/* Every supported profile must admit the single-file Remacs bundle. */
	CHECK(profile.source_max >= 256U * KIB && profile.repl_source_max != 0);
	CHECK(profile.gc_nursery_size != 0 && profile.gc_graduate_size != 0);
	CHECK(profile.gc_tenure_size != 0 && profile.jit_code_size != 0);
	return 0;
}

int
main(void)
{
	struct boot98_noct_memory_profile profile;
	int result;

	result = check_profile(4U * MIB, 0, BOOT98_NOCT_MEMORY_5, 5,
			       1U * MIB, 4U * MIB - 64U * KIB);
	CHECK(result == 0);
	result = check_profile(7U * MIB, 0, BOOT98_NOCT_MEMORY_8, 8,
			       1U * MIB, 7U * MIB - 64U * KIB);
	CHECK(result == 0);
	result = check_profile(14U * MIB, 0, BOOT98_NOCT_MEMORY_16, 16,
			       1U * MIB, 14U * MIB - 64U * KIB);
	CHECK(result == 0);
	result = check_profile(14U * MIB, 1, BOOT98_NOCT_MEMORY_16, 17,
			       1U * MIB, 14U * MIB - 64U * KIB);
	CHECK(result == 0);
	result = check_profile(14U * MIB, 16, BOOT98_NOCT_MEMORY_32, 32,
			       16U * MIB, 16U * MIB - 64U * KIB);
	CHECK(result == 0);
	result = check_profile(14U * MIB, 48, BOOT98_NOCT_MEMORY_64, 64,
			       16U * MIB, 48U * MIB - 64U * KIB);
	CHECK(result == 0);
	result = check_profile(14U * MIB, 49, BOOT98_NOCT_MEMORY_LARGE, 65,
			       16U * MIB, 49U * MIB - 64U * KIB);
	CHECK(result == 0);
	result = check_profile(UINT32_MAX, UINT32_MAX,
			       BOOT98_NOCT_MEMORY_LARGE, UINT32_MAX,
			       16U * MIB, 64U * MIB);
	CHECK(result == 0);
	CHECK(!boot98_noct_select_memory(64U * KIB, 0, &profile));
	CHECK(!boot98_noct_select_memory(4U * MIB, 0, NULL));
	return 0;
}
