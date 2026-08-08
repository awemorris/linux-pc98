/*
 * PC-98 Bootstrap Environment filesystem-backed stdio
 * Copyright (C) 2026 Awe Morris
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "boot98-stdio-fs.h"
#include "../boot98-env.h"
#include "../boot98-fs.h"

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define STREAM_READ 1U
#define STREAM_WRITE 2U

/* Small standalone libc tests do not link the Boots environment store. */
extern const char *boot98_env_get(
	const struct boot98_environment *environment,
	const char *name) __attribute__((weak));

struct filesystem_stream {
	FILE stream;
	struct boot98_file file;
	struct filesystem_stream *next;
};

static struct boot98_filesystem *active_filesystem;
static struct boot98_environment *active_environment;
static struct filesystem_stream *open_streams;

static int result_errno(enum boot98_fs_result result)
{
	switch (result) {
	case BOOT98_FS_NOT_FOUND:
		return ENOENT;
	case BOOT98_FS_READ_ONLY:
		return EROFS;
	case BOOT98_FS_NO_SPACE:
		return ENOSPC;
	case BOOT98_FS_INVALID_PATH:
	case BOOT98_FS_INVALID_ARGUMENT:
		return EINVAL;
	default:
		return EIO;
	}
}

static struct filesystem_stream *filesystem_stream(FILE *stream)
{
	struct filesystem_stream *candidate;

	for (candidate = open_streams; candidate != NULL;
	     candidate = candidate->next)
		if (&candidate->stream == stream)
			return candidate;
	return NULL;
}

void boot98_stdio_set_filesystem(struct boot98_filesystem *filesystem)
{
	active_filesystem = filesystem;
}

void boot98_stdio_set_environment(struct boot98_environment *environment)
{
	active_environment = environment;
}

FILE *fopen(const char *path, const char *mode)
{
	struct filesystem_stream *handle;
	enum boot98_fs_result result;
	unsigned flags;

	if (path == NULL || mode == NULL || active_filesystem == NULL) {
		errno = path == NULL || mode == NULL ? EINVAL : ENOENT;
		return NULL;
	}
	if (!strcmp(mode, "r") || !strcmp(mode, "rb"))
		flags = STREAM_READ;
	else if (!strcmp(mode, "w") || !strcmp(mode, "wb"))
		flags = STREAM_WRITE;
	else {
		errno = EINVAL;
		return NULL;
	}
	handle = malloc(sizeof(*handle));
	if (handle == NULL) {
		errno = ENOMEM;
		return NULL;
	}
	memset(handle, 0, sizeof(*handle));
	result = flags == STREAM_WRITE ?
		boot98_fs_create_result(active_filesystem, path, &handle->file) :
		boot98_fs_open_result(active_filesystem, path, &handle->file);
	if (result != BOOT98_FS_OK) {
		errno = result_errno(result);
		free(handle);
		return NULL;
	}
	handle->stream.context = &handle->file;
	handle->stream.mode = flags;
	handle->next = open_streams;
	open_streams = handle;
	return &handle->stream;
}

int fflush(FILE *stream)
{
	struct filesystem_stream *handle;
	enum boot98_fs_result result;
	int failed = 0;

	if (stream == NULL) {
		for (handle = open_streams; handle != NULL; handle = handle->next)
			if ((handle->stream.mode & STREAM_WRITE) &&
			    fflush(&handle->stream) == EOF)
				failed = 1;
		return failed ? EOF : 0;
	}
	if (stream == stdout || stream == stderr)
		return 0;
	handle = filesystem_stream(stream);
	if (handle == NULL) {
		errno = EINVAL;
		return EOF;
	}
	if (!(stream->mode & STREAM_WRITE))
		return 0;
	result = boot98_file_flush_result(&handle->file);
	if (result != BOOT98_FS_OK) {
		stream->error = 1;
		errno = result_errno(result);
		return EOF;
	}
	return 0;
}

int fclose(FILE *stream)
{
	struct filesystem_stream **link = &open_streams;
	struct filesystem_stream *handle;
	int result;

	while (*link != NULL && &(*link)->stream != stream)
		link = &(*link)->next;
	if (*link == NULL) {
		errno = EINVAL;
		return EOF;
	}
	handle = *link;
	result = fflush(stream);
	*link = handle->next;
	memset(handle, 0, sizeof(*handle));
	free(handle);
	return result;
}

int boot98_stdio_close_all(void)
{
	int failed = 0;

	while (open_streams != NULL)
		if (fclose(&open_streams->stream) == EOF)
			failed = 1;
	return failed ? EOF : 0;
}

size_t fread(void *buffer, size_t size, size_t count, FILE *stream)
{
	struct filesystem_stream *handle = filesystem_stream(stream);
	uint64_t available;
	size_t total, bytes;
	enum boot98_fs_result result;

	if (size != 0 && count > (size_t)-1 / size) {
		errno = EINVAL;
		return 0;
	}
	total = size * count;
	if (total == 0)
		return 0;
	if (buffer == NULL || handle == NULL || !(stream->mode & STREAM_READ)) {
		if (stream != NULL)
			stream->error = 1;
		errno = EINVAL;
		return 0;
	}
	available = stream->position < handle->file.size ?
		handle->file.size - stream->position : 0;
	bytes = available < total ? (size_t)available : total;
	if (bytes == 0) {
		stream->eof = 1;
		return 0;
	}
	result = boot98_file_read_result(&handle->file, stream->position,
					 buffer, (uint32_t)bytes, NULL, NULL);
	if (result != BOOT98_FS_OK) {
		stream->error = 1;
		errno = result_errno(result);
		return 0;
	}
	stream->position += bytes;
	if (bytes < total)
		stream->eof = 1;
	return bytes / size;
}

size_t fwrite(const void *buffer, size_t size, size_t count, FILE *stream)
{
	struct filesystem_stream *handle;
	size_t total;
	enum boot98_fs_result result;

	if (size != 0 && count > (size_t)-1 / size) {
		errno = EINVAL;
		return 0;
	}
	total = size * count;
	if (stream == stdout || stream == stderr)
		return boot98_console_write_bytes(buffer, total) == total ? count : 0;
	if (total == 0)
		return 0;
	handle = filesystem_stream(stream);
	if (buffer == NULL || handle == NULL || !(stream->mode & STREAM_WRITE)) {
		if (stream != NULL)
			stream->error = 1;
		errno = EINVAL;
		return 0;
	}
	result = boot98_file_write_result(&handle->file, stream->position,
					  buffer, (uint32_t)total);
	if (result != BOOT98_FS_OK) {
		stream->error = 1;
		errno = result_errno(result);
		return 0;
	}
	stream->position += total;
	return count;
}

int getc(FILE *stream)
{
	unsigned char byte;

	return fread(&byte, 1, 1, stream) == 1 ? byte : EOF;
}

int fseek(FILE *stream, long offset, int whence)
{
	struct filesystem_stream *handle = filesystem_stream(stream);
	uint64_t base, position;

	if (handle == NULL || (whence != SEEK_SET && whence != SEEK_CUR &&
				 whence != SEEK_END)) {
		errno = EINVAL;
		return -1;
	}
	base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? stream->position :
		handle->file.size;
	if (offset < 0) {
		uint64_t distance = (uint64_t)(-(offset + 1L)) + 1U;

		if (distance > base) {
			errno = EINVAL;
			return -1;
		}
		position = base - distance;
	} else {
		if ((uint64_t)offset > UINT64_MAX - base) {
			errno = EOVERFLOW;
			return -1;
		}
		position = base + (uint64_t)offset;
	}
	stream->position = position;
	stream->eof = 0;
	return 0;
}

long ftell(FILE *stream)
{
	if (filesystem_stream(stream) == NULL) {
		errno = EINVAL;
		return -1;
	}
	if (stream->position > (uint64_t)LONG_MAX) {
		errno = EOVERFLOW;
		return -1;
	}
	return (long)stream->position;
}

char *fgets(char *buffer, int size, FILE *stream)
{
	int index = 0;

	if (buffer == NULL || size <= 0) {
		errno = EINVAL;
		return NULL;
	}
	while (index + 1 < size) {
		int character = getc(stream);

		if (character == EOF)
			break;
		buffer[index++] = (char)character;
		if (character == '\n')
			break;
	}
	if (index == 0)
		return NULL;
	buffer[index] = '\0';
	return buffer;
}

int access(const char *path, int mode)
{
	struct boot98_dirent entry;
	enum boot98_fs_result result;

	if (active_filesystem == NULL || path == NULL || mode != F_OK) {
		errno = path == NULL || mode != F_OK ? EINVAL : ENOENT;
		return -1;
	}
	result = boot98_fs_stat_result(active_filesystem, path, &entry);
	if (result != BOOT98_FS_OK) {
		errno = result_errno(result);
		return -1;
	}
	return 0;
}

/*
 * Boots currently exposes one mounted filesystem rooted at "/".  Noct's
 * FileUtil API expects the POSIX current-directory entry points, so provide
 * the root-only semantics here rather than teaching the portable Noct core
 * about the boot environment.
 */
char *getcwd(char *buffer, size_t size)
{
	if (buffer == NULL || size < 2) {
		errno = buffer == NULL ? EINVAL : ERANGE;
		return NULL;
	}
	buffer[0] = '/';
	buffer[1] = '\0';
	return buffer;
}

int chdir(const char *path)
{
	if (path == NULL) {
		errno = EINVAL;
		return -1;
	}
	if (path[0] == '\0' ||
	    (path[0] == '.' && path[1] == '\0') ||
	    (path[0] == '/' && path[1] == '\0') ||
	    (path[0] == '\\' && path[1] == '\0'))
		return 0;
	errno = ENOENT;
	return -1;
}

char *getenv(const char *name)
{
	if (boot98_env_get == NULL)
		return NULL;
	return (char *)boot98_env_get(active_environment, name);
}
