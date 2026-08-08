/*
 * Boots persistent environment store
 * Copyright (C) 2026 Awe Morris
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "boot98-env.h"

#include <string.h>

static int
valid_name(const char *name, size_t *length)
{
	size_t index;

	if (name == NULL || name[0] == '\0' ||
	    !((name[0] >= 'A' && name[0] <= 'Z') ||
	      (name[0] >= 'a' && name[0] <= 'z') || name[0] == '_'))
		return 0;
	for (index = 1; name[index] != '\0'; index++) {
		char ch = name[index];

		if (index >= BOOT98_ENV_NAME_MAX ||
		    !((ch >= 'A' && ch <= 'Z') ||
		      (ch >= 'a' && ch <= 'z') ||
		      (ch >= '0' && ch <= '9') || ch == '_'))
			return 0;
	}
	*length = index;
	return 1;
}

int
boot98_env_name_valid(const char *name)
{
	size_t length;

	return valid_name(name, &length);
}

static int
find_entry(const struct boot98_environment *environment, const char *name,
	   size_t *offset, size_t *span)
{
	size_t position = 0;

	while (position < environment->used) {
		const char *entry_name = environment->storage + position;
		size_t name_length = strlen(entry_name);
		const char *entry_value = entry_name + name_length + 1U;
		size_t entry_span = name_length + 1U + strlen(entry_value) + 1U;

		if (strcmp(entry_name, name) == 0) {
			if (offset != NULL)
				*offset = position;
			if (span != NULL)
				*span = entry_span;
			return 1;
		}
		position += entry_span;
	}
	return 0;
}

void
boot98_env_init(struct boot98_environment *environment)
{
	if (environment != NULL)
		memset(environment, 0, sizeof(*environment));
}

const char *
boot98_env_get(const struct boot98_environment *environment, const char *name)
{
	size_t offset;

	if (environment == NULL || name == NULL ||
	    !find_entry(environment, name, &offset, NULL))
		return NULL;
	return environment->storage + offset + strlen(name) + 1U;
}

int
boot98_env_set(struct boot98_environment *environment, const char *name,
	       const char *value)
{
	size_t name_length;
	size_t value_length;
	size_t new_span;
	size_t offset;
	size_t old_span = 0;
	size_t tail;
	int found;

	if (environment == NULL || value == NULL ||
	    !valid_name(name, &name_length))
		return 0;
	value_length = strnlen(value, BOOT98_ENV_VALUE_MAX + 1U);
	if (value_length > BOOT98_ENV_VALUE_MAX)
		return 0;
	new_span = name_length + 1U + value_length + 1U;
	found = find_entry(environment, name, &offset, &old_span);
	if (!found) {
		if (environment->count >= BOOT98_ENV_MAX_ENTRIES)
			return 0;
		offset = environment->used;
	}
	if ((size_t)environment->used - old_span + new_span >
	    BOOT98_ENV_STORAGE_SIZE)
		return 0;

	tail = (size_t)environment->used - offset - old_span;
	if (old_span != new_span)
		memmove(environment->storage + offset + new_span,
			environment->storage + offset + old_span, tail);
	memcpy(environment->storage + offset, name, name_length + 1U);
	memcpy(environment->storage + offset + name_length + 1U, value,
	       value_length + 1U);
	environment->used = (uint16_t)((size_t)environment->used - old_span +
				       new_span);
	if (!found)
		environment->count++;
	return 1;
}

int
boot98_env_unset(struct boot98_environment *environment, const char *name)
{
	size_t offset;
	size_t span;
	size_t tail;

	if (environment == NULL || name == NULL ||
	    !find_entry(environment, name, &offset, &span))
		return 0;
	tail = (size_t)environment->used - offset - span;
	memmove(environment->storage + offset, environment->storage + offset + span,
		tail);
	environment->used = (uint16_t)((size_t)environment->used - span);
	environment->count--;
	return 1;
}

size_t
boot98_env_count(const struct boot98_environment *environment)
{
	return environment != NULL ? environment->count : 0;
}

int
boot98_env_at(const struct boot98_environment *environment, size_t index,
	      const char **name, const char **value)
{
	size_t position = 0;
	size_t current = 0;

	if (environment == NULL || name == NULL || value == NULL ||
	    index >= environment->count)
		return 0;
	while (position < environment->used) {
		const char *entry_name = environment->storage + position;
		const char *entry_value = entry_name + strlen(entry_name) + 1U;

		if (current++ == index) {
			*name = entry_name;
			*value = entry_value;
			return 1;
		}
		position = (size_t)(entry_value - environment->storage) +
			   strlen(entry_value) + 1U;
	}
	return 0;
}
