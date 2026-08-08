/*
 * Boots persistent environment store
 * Copyright (C) 2026 Awe Morris
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef BOOT98_ENV_H
#define BOOT98_ENV_H

#include <stddef.h>
#include <stdint.h>

#define BOOT98_ENV_STORAGE_SIZE 4096U
#define BOOT98_ENV_MAX_ENTRIES 32U
#define BOOT98_ENV_NAME_MAX 31U
#define BOOT98_ENV_VALUE_MAX 255U

/*
 * Entries are stored as consecutive NAME\0VALUE\0 pairs.  The store belongs
 * to Boots rather than a Noct VM, so values survive script and REPL teardown.
 */
struct boot98_environment {
	uint16_t used;
	uint8_t count;
	uint8_t reserved;
	char storage[BOOT98_ENV_STORAGE_SIZE];
};

void boot98_env_init(struct boot98_environment *environment);
int boot98_env_name_valid(const char *name);
const char *boot98_env_get(const struct boot98_environment *environment,
			   const char *name);
int boot98_env_set(struct boot98_environment *environment, const char *name,
		   const char *value);
int boot98_env_unset(struct boot98_environment *environment, const char *name);
size_t boot98_env_count(const struct boot98_environment *environment);
int boot98_env_at(const struct boot98_environment *environment, size_t index,
		  const char **name, const char **value);

#endif
