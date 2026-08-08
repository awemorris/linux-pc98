/*
 * Boots mounted-filesystem namespace
 * Copyright (C) 2026 Awe Morris
 * SPDX-License-Identifier: Zlib
 */

#include "boot98-namespace.h"

#include <stddef.h>
#include <string.h>

#define BOOT98_DIRECTORY_ATTRIBUTE 0x10U

struct resolved_path {
	struct boot98_filesystem *filesystem;
	const char *relative;
};

static int mount_index(const struct boot98_namespace *namespace,
		       const char *name, size_t length)
{
	unsigned index;

	if (namespace == NULL || name == NULL)
		return -1;
	for (index = 0; index < namespace->count; index++)
		if (strlen(namespace->mounts[index].name) == length &&
		    !memcmp(namespace->mounts[index].name, name, length))
			return (int)index;
	return -1;
}

static enum boot98_fs_result resolve(struct boot98_namespace *namespace,
				     const char *path,
				     struct resolved_path *resolved)
{
	const char *name;
	const char *separator;
	size_t length;
	int index;

	if (namespace == NULL || path == NULL || resolved == NULL)
		return BOOT98_FS_INVALID_ARGUMENT;
	if (path[0] != '/') {
		if (namespace->default_mount < 0 ||
		    (unsigned)namespace->default_mount >= namespace->count)
			return BOOT98_FS_NOT_FOUND;
		resolved->filesystem =
			&namespace->mounts[namespace->default_mount].filesystem;
		resolved->relative = path;
		if (path[0] == '.' && path[1] == '/')
			resolved->relative += 2;
		return BOOT98_FS_OK;
	}
	name = path + 1;
	separator = strchr(name, '/');
	length = separator != NULL ? (size_t)(separator - name) : strlen(name);
	if (length == 0)
		return BOOT98_FS_INVALID_PATH;
	index = mount_index(namespace, name, length);
	if (index < 0)
		return BOOT98_FS_NOT_FOUND;
	resolved->filesystem = &namespace->mounts[index].filesystem;
	resolved->relative = separator != NULL ? separator + 1 : "";
	return BOOT98_FS_OK;
}

static void lower_ascii(char *text)
{
	while (*text != '\0') {
		if (*text >= 'A' && *text <= 'Z')
			*text = (char)(*text - 'A' + 'a');
		text++;
	}
}

static const char *directory_path(const char *path,
				  char normalized[BOOT98_PATH_MAX])
{
	size_t length = strlen(path);

	if (length >= BOOT98_PATH_MAX)
		return NULL;
	memcpy(normalized, path, length + 1U);
	/* Noct and ordinary UNIX callers spell directories with a trailing
	 * slash.  FAT's internal resolver expects the final component without
	 * one, so normalize that representation at the namespace boundary. */
	while (length > 0 && normalized[length - 1U] == '/')
		normalized[--length] = '\0';
	return normalized;
}

void boot98_namespace_init(struct boot98_namespace *namespace)
{
	if (namespace == NULL)
		return;
	memset(namespace, 0, sizeof(*namespace));
	namespace->default_mount = -1;
}

int boot98_namespace_mount(struct boot98_namespace *namespace,
			   const char *name,
			   const struct boot98_filesystem *filesystem)
{
	size_t length;
	int existing;
	unsigned index;

	if (namespace == NULL || name == NULL || filesystem == NULL ||
	    filesystem->driver == NULL)
		return 0;
	length = strlen(name);
	if (length == 0 || length >= BOOT98_NAMESPACE_NAME_MAX ||
	    strchr(name, '/') != NULL || strchr(name, '\\') != NULL)
		return 0;
	existing = mount_index(namespace, name, length);
	if (existing >= 0)
		index = (unsigned)existing;
	else {
		if (namespace->count >= BOOT98_NAMESPACE_MAX_MOUNTS)
			return 0;
		index = namespace->count++;
	}
	memset(&namespace->mounts[index], 0, sizeof(namespace->mounts[index]));
	memcpy(namespace->mounts[index].name, name, length + 1U);
	namespace->mounts[index].filesystem = *filesystem;
	if (namespace->default_mount < 0)
		namespace->default_mount = (int)index;
	return 1;
}

int boot98_namespace_set_default(struct boot98_namespace *namespace,
				 const char *name)
{
	int index;

	if (name == NULL)
		return 0;
	index = mount_index(namespace, name, strlen(name));
	if (index < 0)
		return 0;
	namespace->default_mount = index;
	return 1;
}

const char *boot98_namespace_default_name(
	const struct boot98_namespace *namespace)
{
	if (namespace == NULL || namespace->default_mount < 0 ||
	    (unsigned)namespace->default_mount >= namespace->count)
		return NULL;
	return namespace->mounts[namespace->default_mount].name;
}

enum boot98_fs_result boot98_namespace_open_result(
	struct boot98_namespace *namespace, const char *path,
	struct boot98_file *file)
{
	struct resolved_path resolved;
	enum boot98_fs_result result = resolve(namespace, path, &resolved);

	return result != BOOT98_FS_OK ? result :
		boot98_fs_open_result(resolved.filesystem, resolved.relative, file);
}

enum boot98_fs_result boot98_namespace_create_result(
	struct boot98_namespace *namespace, const char *path,
	struct boot98_file *file)
{
	struct resolved_path resolved;
	enum boot98_fs_result result = resolve(namespace, path, &resolved);

	return result != BOOT98_FS_OK ? result :
		boot98_fs_create_result(resolved.filesystem, resolved.relative,
					file);
}

enum boot98_fs_result boot98_namespace_stat_result(
	struct boot98_namespace *namespace, const char *path,
	struct boot98_dirent *entry)
{
	struct resolved_path resolved;
	char normalized[BOOT98_PATH_MAX];
	const char *relative;
	enum boot98_fs_result result;

	if (namespace == NULL || path == NULL || entry == NULL)
		return BOOT98_FS_INVALID_ARGUMENT;
	if (!strcmp(path, "/")) {
		memset(entry, 0, sizeof(*entry));
		entry->attributes = BOOT98_DIRECTORY_ATTRIBUTE;
		return BOOT98_FS_OK;
	}
	result = resolve(namespace, path, &resolved);
	if (result != BOOT98_FS_OK)
		return result;
	if (resolved.relative[0] == '\0') {
		memset(entry, 0, sizeof(*entry));
		entry->attributes = BOOT98_DIRECTORY_ATTRIBUTE;
		return BOOT98_FS_OK;
	}
	relative = directory_path(resolved.relative, normalized);
	if (relative == NULL)
		return BOOT98_FS_INVALID_PATH;
	result = boot98_fs_stat_result(resolved.filesystem, relative, entry);
	if (result == BOOT98_FS_OK)
		lower_ascii(entry->name);
	return result;
}

enum boot98_fs_result boot98_namespace_readdir_result(
	struct boot98_namespace *namespace, const char *path, unsigned index,
	struct boot98_dirent *entry)
{
	struct resolved_path resolved;
	char normalized[BOOT98_PATH_MAX];
	const char *relative;
	enum boot98_fs_result result;

	if (namespace == NULL || path == NULL || entry == NULL)
		return BOOT98_FS_INVALID_ARGUMENT;
	if (!strcmp(path, "/")) {
		if (index >= namespace->count)
			return BOOT98_FS_NOT_FOUND;
		memset(entry, 0, sizeof(*entry));
		strncpy(entry->name, namespace->mounts[index].name,
			sizeof(entry->name) - 1U);
		entry->attributes = BOOT98_DIRECTORY_ATTRIBUTE;
		return BOOT98_FS_OK;
	}
	result = resolve(namespace, path, &resolved);
	if (result != BOOT98_FS_OK)
		return result;
	relative = directory_path(resolved.relative, normalized);
	if (relative == NULL)
		return BOOT98_FS_INVALID_PATH;
	result = boot98_fs_readdir_result(resolved.filesystem, relative, index,
					  entry);
	if (result == BOOT98_FS_OK)
		lower_ascii(entry->name);
	return result;
}
