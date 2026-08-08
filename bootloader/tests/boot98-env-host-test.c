/*
 * Boots persistent environment store tests
 * Copyright (C) 2026 Awe Morris
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "boot98-env.h"

#include <stdio.h>
#include <string.h>

static int
basic_test(void)
{
	struct boot98_environment environment;
	const char *name;
	const char *value;

	boot98_env_init(&environment);
	if (boot98_env_count(&environment) != 0 ||
	    boot98_env_get(&environment, "MODE") != NULL ||
	    !boot98_env_set(&environment, "MODE", "safe") ||
	    !boot98_env_set(&environment, "ROOT", "/dev/sda2") ||
	    strcmp(boot98_env_get(&environment, "MODE"), "safe") != 0 ||
	    !boot98_env_set(&environment, "MODE", "normal") ||
	    strcmp(boot98_env_get(&environment, "MODE"), "normal") != 0 ||
	    boot98_env_count(&environment) != 2 ||
	    !boot98_env_at(&environment, 1, &name, &value) ||
	    strcmp(name, "ROOT") != 0 || strcmp(value, "/dev/sda2") != 0 ||
	    !boot98_env_unset(&environment, "MODE") ||
	    boot98_env_get(&environment, "MODE") != NULL ||
	    boot98_env_unset(&environment, "MODE"))
		return 0;
	return 1;
}

static int
limits_test(void)
{
	struct boot98_environment environment;
	char name[BOOT98_ENV_NAME_MAX + 2U];
	char value[BOOT98_ENV_VALUE_MAX + 2U];
	unsigned index;

	memset(value, 'x', sizeof(value));
	value[BOOT98_ENV_VALUE_MAX] = '\0';
	boot98_env_init(&environment);
	if (boot98_env_set(&environment, "", "x") ||
	    boot98_env_set(&environment, "9BAD", "x") ||
	    boot98_env_set(&environment, "BAD-NAME", "x"))
		return 0;
	memset(name, 'A', sizeof(name));
	name[BOOT98_ENV_NAME_MAX + 1U] = '\0';
	if (boot98_env_set(&environment, name, "x"))
		return 0;
	value[BOOT98_ENV_VALUE_MAX] = 'x';
	value[BOOT98_ENV_VALUE_MAX + 1U] = '\0';
	if (boot98_env_set(&environment, "TOO_LONG", value))
		return 0;

	boot98_env_init(&environment);
	for (index = 0; index < BOOT98_ENV_MAX_ENTRIES; index++) {
		name[0] = 'V';
		name[1] = (char)('A' + index / 26U);
		name[2] = (char)('A' + index % 26U);
		name[3] = '\0';
		if (!boot98_env_set(&environment, name, "x"))
			return 0;
	}
	if (boot98_env_set(&environment, "EXTRA", "x") ||
	    boot98_env_count(&environment) != BOOT98_ENV_MAX_ENTRIES)
		return 0;

	boot98_env_init(&environment);
	value[BOOT98_ENV_VALUE_MAX] = '\0';
	if (!boot98_env_set(&environment, "KEEP", "old"))
		return 0;
	for (index = 0; ; index++) {
		name[0] = 'F';
		name[1] = (char)('A' + index / 26U);
		name[2] = (char)('A' + index % 26U);
		name[3] = '\0';
		if (!boot98_env_set(&environment, name, value))
			break;
	}
	if (index == 0 || boot98_env_set(&environment, "KEEP", value) ||
	    strcmp(boot98_env_get(&environment, "KEEP"), "old") != 0)
		return 0;
	return 1;
}

int
main(void)
{
	if (!basic_test() || !limits_test())
		return 1;
	puts("Boots environment host tests: PASS");
	return 0;
}
