/*
 * Boots mounted-filesystem namespace
 * Copyright (C) 2026 Awe Morris
 * SPDX-License-Identifier: Zlib
 */

#ifndef BOOT98_NAMESPACE_H
#define BOOT98_NAMESPACE_H

#include "boot98-fs.h"

#define BOOT98_NAMESPACE_MAX_MOUNTS 12U
#define BOOT98_NAMESPACE_NAME_MAX 16U

struct boot98_namespace_mount {
	char name[BOOT98_NAMESPACE_NAME_MAX];
	struct boot98_filesystem filesystem;
};

struct boot98_namespace {
	struct boot98_namespace_mount mounts[BOOT98_NAMESPACE_MAX_MOUNTS];
	unsigned count;
	int default_mount;
};

void boot98_namespace_init(struct boot98_namespace *namespace);
int boot98_namespace_mount(struct boot98_namespace *namespace,
			   const char *name,
			   const struct boot98_filesystem *filesystem);
int boot98_namespace_set_default(struct boot98_namespace *namespace,
				 const char *name);
const char *boot98_namespace_default_name(
	const struct boot98_namespace *namespace);

enum boot98_fs_result boot98_namespace_open_result(
	struct boot98_namespace *namespace, const char *path,
	struct boot98_file *file);
enum boot98_fs_result boot98_namespace_create_result(
	struct boot98_namespace *namespace, const char *path,
	struct boot98_file *file);
enum boot98_fs_result boot98_namespace_stat_result(
	struct boot98_namespace *namespace, const char *path,
	struct boot98_dirent *entry);
enum boot98_fs_result boot98_namespace_readdir_result(
	struct boot98_namespace *namespace, const char *path, unsigned index,
	struct boot98_dirent *entry);

#endif
