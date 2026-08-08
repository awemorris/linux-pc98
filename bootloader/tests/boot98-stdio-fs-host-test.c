/* Host-side tests for BOOT98 filesystem-backed stdio. */

#include "boot98-fs.h"
#include "boot98-env.h"
#include "boot98-namespace.h"
#include "libc/boot98-heap.h"
#include "libc/boot98-stdio-fs.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static unsigned char arena[128 * 1024];
static unsigned char contents[4096];
static uint64_t content_size;
static int exists;
static unsigned flushes;

static enum boot98_fs_result probe(const struct boot98_volume *volume)
{
	(void)volume;
	return BOOT98_FS_OK;
}

static enum boot98_fs_result mount(struct boot98_filesystem *filesystem)
{
	(void)filesystem;
	return BOOT98_FS_OK;
}

static enum boot98_fs_result populate(const char *path, struct boot98_file *file)
{
	if (strcmp(path, "/TEST.TXT") && strcmp(path, "TEST.TXT"))
		return BOOT98_FS_NOT_FOUND;
	if (!exists)
		return BOOT98_FS_NOT_FOUND;
	file->size = content_size;
	return BOOT98_FS_OK;
}

static enum boot98_fs_result create(struct boot98_filesystem *filesystem,
				    const char *path,
				    struct boot98_file *file)
{
	(void)filesystem;
	if (strcmp(path, "/TEST.TXT") && strcmp(path, "TEST.TXT"))
		return BOOT98_FS_INVALID_PATH;
	exists = 1;
	content_size = 0;
	file->size = 0;
	return BOOT98_FS_OK;
}

static enum boot98_fs_result open_file(struct boot98_filesystem *filesystem,
				       const char *path,
				       struct boot98_file *file)
{
	(void)filesystem;
	return populate(path, file);
}

static enum boot98_fs_result read_file(struct boot98_file *file,
		uint64_t offset, void *buffer, uint32_t length,
		boot98_read_progress_t progress, void *progress_context)
{
	(void)file;
	(void)progress;
	(void)progress_context;
	memcpy(buffer, contents + offset, length);
	return BOOT98_FS_OK;
}

static enum boot98_fs_result write_file(struct boot98_file *file,
		uint64_t offset, const void *buffer, uint32_t length)
{
	uint64_t end = offset + length;

	if (end > sizeof(contents))
		return BOOT98_FS_NO_SPACE;
	if (offset > content_size)
		memset(contents + content_size, 0, (size_t)(offset - content_size));
	memcpy(contents + offset, buffer, length);
	if (end > content_size)
		content_size = end;
	file->size = content_size;
	return BOOT98_FS_OK;
}

static enum boot98_fs_result truncate_file(struct boot98_file *file,
					   uint64_t size)
{
	if (size > sizeof(contents))
		return BOOT98_FS_NO_SPACE;
	if (size > content_size)
		memset(contents + content_size, 0, (size_t)(size - content_size));
	content_size = size;
	file->size = size;
	return BOOT98_FS_OK;
}

static enum boot98_fs_result flush_file(struct boot98_file *file)
{
	(void)file;
	flushes++;
	return BOOT98_FS_OK;
}

static enum boot98_fs_result readdir(struct boot98_filesystem *filesystem,
		const char *path, unsigned index, struct boot98_dirent *entry)
{
	(void)filesystem;
	if (index || !exists)
		return BOOT98_FS_NOT_FOUND;
	if (!strcmp(path, "HOME") || !strcmp(path, "home"))
		strcpy(entry->name, "COMPLETE.TXT");
	else if (!*path || !strcmp(path, "/"))
		strcpy(entry->name, "TEST.TXT");
	else
		return BOOT98_FS_NOT_FOUND;
	entry->size = content_size;
	return BOOT98_FS_OK;
}

static enum boot98_fs_result stat_file(struct boot98_filesystem *filesystem,
		const char *path, struct boot98_dirent *entry)
{
	struct boot98_file file;
	enum boot98_fs_result result;

	(void)filesystem;
	if (!strcmp(path, "HOME") || !strcmp(path, "home")) {
		strcpy(entry->name, "HOME");
		entry->size = 0;
		entry->attributes = 0x10;
		return BOOT98_FS_OK;
	}
	result = populate(path, &file);
	if (result != BOOT98_FS_OK)
		return result;
	strcpy(entry->name, "TEST.TXT");
	entry->size = file.size;
	return BOOT98_FS_OK;
}

static int dummy_read(const void *context, uint32_t lba, void *buffer)
{
	(void)context;
	(void)lba;
	memset(buffer, 0, 512);
	return 1;
}

static const struct boot98_filesystem_driver driver = {
	.name = "memory",
	.probe = probe,
	.mount = mount,
	.create = create,
	.open = open_file,
	.read = read_file,
	.write = write_file,
	.truncate = truncate_file,
	.flush = flush_file,
	.readdir = readdir,
	.stat = stat_file,
};

int main(void)
{
	const struct boot98_filesystem_driver *drivers[] = { &driver };
	struct boot98_volume volume = {
		.sector_size = 512,
		.read = dummy_read,
	};
	struct boot98_filesystem filesystem;
	struct boot98_namespace namespace;
	struct boot98_dirent entry;
	struct boot98_environment environment;
	FILE *file;
	char line[32];
	char bytes[4];

	boot98_heap_init(arena, sizeof(arena));
	boot98_env_init(&environment);
	assert(boot98_env_set(&environment, "HOME", "HOME"));
	assert(boot98_fs_mount(&filesystem, &volume, drivers, 1));
	boot98_stdio_set_filesystem(&filesystem);
	boot98_stdio_set_environment(&environment);
	assert(!strcmp(getenv("HOME"), "HOME"));
	assert(getenv("MISSING") == NULL);
	assert(access("/TEST.TXT", F_OK) == -1);
	file = fopen("/TEST.TXT", "wb");
	assert(file != NULL);
	assert(fprintf(file, "line %d\n", 42) == 8);
	assert(ftell(file) == 8);
	assert(fseek(file, 10, SEEK_SET) == 0);
	assert(fwrite("xy", 1, 2, file) == 2);
	assert(fclose(file) == 0 && flushes == 1);
	assert(content_size == 12 && contents[8] == 0 && contents[9] == 0);
	assert(access("/TEST.TXT", F_OK) == 0);

	file = fopen("/TEST.TXT", "rb");
	assert(file != NULL);
	assert(fgets(line, sizeof(line), file) == line);
	assert(!strcmp(line, "line 42\n"));
	assert(fseek(file, -4, SEEK_END) == 0);
	assert(ftell(file) == 8);
	assert(fread(bytes, 1, sizeof(bytes), file) == sizeof(bytes));
	assert(bytes[0] == 0 && bytes[1] == 0 && bytes[2] == 'x' &&
	       bytes[3] == 'y');
	assert(getc(file) == EOF);
	assert(fclose(file) == 0);

	assert(fopen("/TEST.TXT", "rb") != NULL);
	assert(boot98_stdio_close_all() == 0);

	boot98_namespace_init(&namespace);
	assert(boot98_namespace_mount(&namespace, "disk1", &filesystem));
	assert(boot98_namespace_set_default(&namespace, "disk1"));
	assert(boot98_namespace_readdir_result(&namespace, "/disk1", 0,
					       &entry) == BOOT98_FS_OK);
	assert(!strcmp(entry.name, "test.txt"));
	assert(boot98_namespace_readdir_result(&namespace, "/disk1/home/", 0,
					       &entry) == BOOT98_FS_OK);
	assert(!strcmp(entry.name, "complete.txt"));
	boot98_stdio_set_namespace(&namespace);
	assert(getcwd(line, sizeof(line)) == line);
	assert(!strcmp(line, "/disk1"));
	assert(chdir("/disk1/home") == 0);
	assert(getcwd(line, sizeof(line)) == line);
	assert(!strcmp(line, "/disk1/home"));
	assert(chdir("/disk1") == 0);
	file = fopen("TEST.TXT", "rb");
	assert(file != NULL);
	assert(fclose(file) == 0);
	boot98_stdio_set_namespace(NULL);
	boot98_stdio_set_filesystem(NULL);
	boot98_stdio_set_environment(NULL);
	assert(boot98_heap_current() == 0 && boot98_heap_validate());
	puts("BOOT98 filesystem stdio host tests: OK");
	return 0;
}
