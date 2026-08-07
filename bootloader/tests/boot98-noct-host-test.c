/*
 * PC-98 Bootstrap Environment Noct lifecycle and M8 NAPI test
 * Copyright (C) 2026 Awe Morris
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "boot98-noct.h"
#include "boot98-noct-napi.h"
#include "boot98-noct-m6-script.h"
#include "boot98-fs.h"
#include "boot98-heap.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define ARENA_SIZE (6U * 1024U * 1024U)
#define OUTPUT_SIZE 1024U
#define SCRIPT_SIZE 8192U
#define SYS_READ 3
#define SYS_WRITE 4
#define SYS_OPEN 5
#define SYS_CLOSE 6
#define SYS_MPROTECT 125
#define OPEN_FLAGS 01101
#define OPEN_MODE 0644

static unsigned char arena[ARENA_SIZE] __attribute__((aligned(4096)));
static unsigned char jit_capture[BOOT98_NOCT_JIT_CODE_MAX];
static size_t jit_capture_length;
static char output[OUTPUT_SIZE];
static size_t output_length;
static struct boot98_filesystem *test_filesystem;
static char ls_source[SCRIPT_SIZE];
static char cp_source[SCRIPT_SIZE];

struct memory_record {
	char name[16];
	unsigned char data[20000];
	uint64_t size;
	unsigned flushes;
	int exists;
};

static struct memory_record records[4];

static struct memory_record *find_record(const char *path, int create)
{
	unsigned index;

	if (*path == '/')
		path++;
	for (index = 0; index < sizeof(records) / sizeof(records[0]); index++)
		if (records[index].exists && !strcmp(records[index].name, path))
			return &records[index];
	if (!create)
		return NULL;
	for (index = 0; index < sizeof(records) / sizeof(records[0]); index++)
		if (!records[index].exists) {
			if (strlen(path) >= sizeof(records[index].name))
				return NULL;
			strcpy(records[index].name, path);
			records[index].exists = 1;
			records[index].size = 0;
			return &records[index];
		}
	return NULL;
}

static enum boot98_fs_result memory_probe(const struct boot98_volume *volume)
{
	(void)volume;
	return BOOT98_FS_OK;
}

static enum boot98_fs_result memory_mount(struct boot98_filesystem *filesystem)
{
	(void)filesystem;
	return BOOT98_FS_OK;
}

static enum boot98_fs_result memory_create(
	struct boot98_filesystem *filesystem, const char *path,
	struct boot98_file *file)
{
	struct memory_record *record = find_record(path, 1);

	(void)filesystem;
	if (record == NULL)
		return BOOT98_FS_NO_SPACE;
	record->size = 0;
	file->private_data[0] = (uint32_t)(record - records);
	file->size = 0;
	return BOOT98_FS_OK;
}

static enum boot98_fs_result memory_open(
	struct boot98_filesystem *filesystem, const char *path,
	struct boot98_file *file)
{
	struct memory_record *record = find_record(path, 0);

	(void)filesystem;
	if (record == NULL)
		return BOOT98_FS_NOT_FOUND;
	file->private_data[0] = (uint32_t)(record - records);
	file->size = record->size;
	return BOOT98_FS_OK;
}

static enum boot98_fs_result memory_read(
	struct boot98_file *file, uint64_t offset, void *buffer, uint32_t length,
	boot98_read_progress_t progress, void *progress_context)
{
	struct memory_record *record = &records[file->private_data[0]];

	(void)progress;
	(void)progress_context;
	if (offset > record->size || length > record->size - offset)
		return BOOT98_FS_IO_ERROR;
	memcpy(buffer, record->data + offset, length);
	return BOOT98_FS_OK;
}

static enum boot98_fs_result memory_write(
	struct boot98_file *file, uint64_t offset, const void *buffer,
	uint32_t length)
{
	struct memory_record *record = &records[file->private_data[0]];
	uint64_t end = offset + length;

	if (end > sizeof(record->data))
		return BOOT98_FS_NO_SPACE;
	if (offset > record->size)
		memset(record->data + record->size, 0,
		       (size_t)(offset - record->size));
	memcpy(record->data + offset, buffer, length);
	if (end > record->size)
		record->size = end;
	file->size = record->size;
	return BOOT98_FS_OK;
}

static enum boot98_fs_result memory_truncate(struct boot98_file *file,
					     uint64_t size)
{
	struct memory_record *record = &records[file->private_data[0]];

	if (size > sizeof(record->data))
		return BOOT98_FS_NO_SPACE;
	record->size = size;
	file->size = size;
	return BOOT98_FS_OK;
}

static enum boot98_fs_result memory_flush(struct boot98_file *file)
{
	records[file->private_data[0]].flushes++;
	return BOOT98_FS_OK;
}

static enum boot98_fs_result memory_readdir(
	struct boot98_filesystem *filesystem, const char *path, unsigned index,
	struct boot98_dirent *entry)
{
	unsigned visible = 0;

	(void)filesystem;
	if (*path && strcmp(path, "/"))
		return BOOT98_FS_INVALID_PATH;
	for (unsigned record = 0;
	     record < sizeof(records) / sizeof(records[0]); record++)
		if (records[record].exists && visible++ == index) {
			strcpy(entry->name, records[record].name);
			entry->size = records[record].size;
			return BOOT98_FS_OK;
		}
	return BOOT98_FS_NOT_FOUND;
}

static enum boot98_fs_result memory_stat(
	struct boot98_filesystem *filesystem, const char *path,
	struct boot98_dirent *entry)
{
	struct memory_record *record = find_record(path, 0);

	(void)filesystem;
	if (record == NULL)
		return BOOT98_FS_NOT_FOUND;
	strcpy(entry->name, record->name);
	entry->size = record->size;
	return BOOT98_FS_OK;
}

static const struct boot98_filesystem_driver memory_driver = {
	.name = "memory",
	.probe = memory_probe,
	.mount = memory_mount,
	.create = memory_create,
	.open = memory_open,
	.read = memory_read,
	.write = memory_write,
	.truncate = memory_truncate,
	.flush = memory_flush,
	.readdir = memory_readdir,
	.stat = memory_stat,
};

static int volume_read(const void *context, uint32_t lba, void *buffer)
{
	(void)context;
	(void)lba;
	memset(buffer, 0, 512);
	return 1;
}

struct mock_platform {
	int clear_count;
	int clear_row;
	int put_row;
	int put_column;
	int put_attribute;
	char put_text[32];
	int cursor_row;
	int cursor_column;
	int cursor_visible;
};

static struct mock_platform mock;
static const char imported_source[] =
	"func imported() { return \"imported\"; }";

static int mock_screen_clear(void *context)
{
	struct mock_platform *platform = context;
	platform->clear_count++;
	return 1;
}

static int mock_screen_clear_row(void *context, unsigned row)
{
	struct mock_platform *platform = context;
	platform->clear_row = (int)row;
	return 1;
}

static int mock_screen_put(void *context, unsigned row, unsigned column,
			   const char *text, uint8_t attribute)
{
	struct mock_platform *platform = context;
	size_t length = strlen(text);
	if (length >= sizeof(platform->put_text))
		return -1;
	platform->put_row = (int)row;
	platform->put_column = (int)column;
	platform->put_attribute = attribute;
	memcpy(platform->put_text, text, length + 1U);
	return (int)length;
}

static int mock_screen_set_cursor(void *context, unsigned row,
				  unsigned column)
{
	struct mock_platform *platform = context;
	platform->cursor_row = (int)row;
	platform->cursor_column = (int)column;
	return 1;
}

static int mock_screen_show_cursor(void *context, int visible)
{
	struct mock_platform *platform = context;
	platform->cursor_visible = visible;
	return 1;
}

static int mock_keyboard_poll(void *context)
{
	(void)context;
	return BOOT98_KEY_LEFT;
}

static int mock_keyboard_read(void *context)
{
	(void)context;
	return 'A';
}

static int mock_file_size(void *context, const char *path, uint32_t *size)
{
	(void)context;
	if (strcmp(path, "LIB.NCT") != 0)
		return 0;
	*size = (uint32_t)strlen(imported_source);
	return 1;
}

static int mock_file_read(void *context, const char *path, uint32_t offset,
			  void *buffer, uint32_t length)
{
	(void)context;
	if (strcmp(path, "LIB.NCT") != 0 ||
	    offset > strlen(imported_source) ||
	    length > strlen(imported_source) - offset)
		return 0;
	memcpy(buffer, imported_source + offset, length);
	return 1;
}

static int mock_directory_read(void *context, const char *path, unsigned index,
			       struct boot98_noct_dirent *entry)
{
	static const struct boot98_noct_dirent entries[] = {
		{ "BOOT.CFG", 7, 0x20 },
		{ "SCRIPTS", 0, 0x10 },
		{ "LIB.NCT", 38, 0x20 },
	};
	(void)context;
	if (strcmp(path, "") != 0 && strcmp(path, "/") != 0)
		return -1;
	if (index >= sizeof(entries) / sizeof(entries[0]))
		return 0;
	*entry = entries[index];
	return 1;
}

static const struct boot98_noct_services mock_services = {
	&mock,
	mock_screen_clear,
	mock_screen_clear_row,
	mock_screen_put,
	mock_screen_set_cursor,
	mock_screen_show_cursor,
	mock_keyboard_poll,
	mock_keyboard_read,
	mock_file_size,
	mock_file_read,
	mock_directory_read,
};

static long
host_syscall3(long number, long argument1, long argument2, long argument3)
{
	long result;

	__asm__ volatile ("int $0x80" : "=a"(result) : "0"(number),
			  "b"(argument1), "c"(argument2), "d"(argument3) :
			  "memory");
	return result;
}

static int
read_host_source(const char *path, char *buffer, size_t capacity)
{
	long descriptor;
	size_t length = 0;

	descriptor = host_syscall3(SYS_OPEN, (long)(uintptr_t)path, 0, 0);
	if (descriptor < 0)
		return 0;
	while (length < capacity - 1U) {
		long count = host_syscall3(SYS_READ, descriptor,
			(long)(uintptr_t)(buffer + length),
			(long)(capacity - 1U - length));

		if (count < 0) {
			(void)host_syscall3(SYS_CLOSE, descriptor, 0, 0);
			return 0;
		}
		if (count == 0)
			break;
		length += (size_t)count;
	}
	if (length == capacity - 1U) {
		char extra;
		long count = host_syscall3(SYS_READ, descriptor,
			(long)(uintptr_t)&extra, 1);

		if (count != 0) {
			(void)host_syscall3(SYS_CLOSE, descriptor, 0, 0);
			return 0;
		}
	}
	buffer[length] = '\0';
	return host_syscall3(SYS_CLOSE, descriptor, 0, 0) == 0;
}

static int
write_capture_file(const char *path)
{
	long descriptor;
	size_t offset = 0;

	descriptor = host_syscall3(SYS_OPEN, (long)(uintptr_t)path,
				   OPEN_FLAGS, OPEN_MODE);
	if (descriptor < 0)
		return 0;
	while (offset < jit_capture_length) {
		long count = host_syscall3(SYS_WRITE, descriptor,
					   (long)(uintptr_t)(jit_capture + offset),
					   (long)(jit_capture_length - offset));
		if (count <= 0) {
			(void)host_syscall3(SYS_CLOSE, descriptor, 0, 0);
			return 0;
		}
		offset += (size_t)count;
	}
	return host_syscall3(SYS_CLOSE, descriptor, 0, 0) == 0;
}

static size_t
capture_output(void *context, const char *bytes, size_t length)
{
	size_t available;

	(void)context;
	available = OUTPUT_SIZE - 1U - output_length;
	if (length > available)
		length = available;
	memcpy(output + output_length, bytes, length);
	output_length += length;
	output[output_length] = '\0';
	return length;
}

static void
capture_jit(void *context, const void *code, size_t length)
{
	(void)context;
	if (length > sizeof(jit_capture)) {
		jit_capture_length = 0;
		return;
	}
	memcpy(jit_capture, code, length);
	jit_capture_length = length;
}

static int
run_case_args(const char *source, int argc, char *const argv[], int jit_enable,
	      enum boot98_noct_status expected, int64_t expected_script_status,
	      const char *expected_output,
	      struct boot98_noct_result *returned_result)
{
	struct boot98_noct_options options;
	struct boot98_noct_result result;
	int success;

	output_length = 0;
	output[0] = '\0';
	options.arena = arena;
	options.arena_size = sizeof(arena);
	options.fail_after = BOOT98_NOCT_NO_FAILURE;
	options.jit_enable = jit_enable;
	options.jit_threshold = 1;
	options.write = capture_output;
	options.write_context = NULL;
	options.observe_jit_code = capture_jit;
	options.jit_context = NULL;
	options.services = &mock_services;
	options.filesystem = test_filesystem;
	success = boot98_noct_run_args("noct-test.nct", source, argc, argv,
				       &options, &result);
	if (success != (expected == BOOT98_NOCT_OK) ||
	    result.status != expected ||
	    result.script_status != expected_script_status) {
		if (output_length != 0)
			(void)host_syscall3(SYS_WRITE, 2,
				(long)(uintptr_t)output, (long)output_length);
		return 10 + (int)result.status;
	}
	if (result.current_after_reset != 0 || boot98_heap_current() != 0 ||
	    result.heap_errors != 0 || !boot98_heap_validate())
		return 20;
	if (expected_output != NULL && strcmp(output, expected_output) != 0) {
		(void)host_syscall3(SYS_WRITE, 2, (long)(uintptr_t)output,
				    (long)output_length);
		(void)host_syscall3(SYS_WRITE, 2, (long)(uintptr_t)"EXPECTED:\n",
				    10);
		(void)host_syscall3(SYS_WRITE, 2,
				    (long)(uintptr_t)expected_output,
				    (long)strlen(expected_output));
		return 30;
	}
	if (expected_output == NULL && output_length == 0)
		return 31;
	if (jit_enable) {
		if (result.jit_code_size != BOOT98_NOCT_JIT_CODE_MAX)
			return 32;
		if (!result.jit_region_released)
			return 34;
		if (jit_capture_length != BOOT98_NOCT_JIT_CODE_MAX)
			return 35;
	} else if (result.jit_code_size != 0 || result.jit_region_released) {
		return 33;
	}
	if (returned_result != NULL)
		*returned_result = result;
	return 0;
}

static int
run_case(const char *source, int jit_enable,
	 enum boot98_noct_status expected, const char *expected_output,
	 struct boot98_noct_result *returned_result)
{
	return run_case_args(source, 0, NULL, jit_enable, expected, 0,
			     expected_output, returned_result);
}

int
main(int argc, char **argv)
{
	static const char syntax_error[] = "func main( {";
	static const char runtime_error[] =
		"func main() { Console.write(1); }";
	static const char argument_script[] =
		"func main(args) { for (arg in args) { "
		"Console.write(\"[\" + arg + \"]\"); } return 7; }";
	static const char zero_argument_script[] =
		"func main() { Console.write(\"zero\"); return 0; }";
	static const char long_status_script[] =
		"func main() { Console.write(\"long\"); return 9L; }";
	static const char bad_signature[] =
		"func main(first, second) { return 0; }";
	static const char napi_script[] =
		"func main() { "
		"Console.print({answer: 42, items: [\"x\", 2]}); "
		"Console.write(\"raw\"); Console.print(\"\"); "
		"Screen.clear(); Screen.clearRow(7); "
		"Console.print(Screen.put(2, 3, \"AB\", 225)); "
		"Screen.setCursor(4, 5); Screen.showCursor(0); "
		"Console.print(Keyboard.poll()); "
		"Console.print(Keyboard.read()); "
		"Console.print(Keyboard.isPrintable(65)); "
		"Console.print(Key.Left); "
		"var entries = Directory.list(\"/\"); "
		"Console.print(entries[0].name); "
		"var stat = Directory.stat(\"/BOOT.CFG\"); "
		"Console.print(stat.size); "
		"Console.print(System.getOSName()); "
		"System.import(\"LIB.NCT\"); Console.print(imported()); "
		"var usage = System.memoryUsage(); "
		"Console.print(usage.arenaSize); return 0; }";
	static const char napi_output[] =
		"{answer: 42, items: [\"x\", 2]}\n"
		"raw\n2\n315\n65\n1\n315\nBOOT.CFG\n7\nPC98BE\n"
		"imported\n6291456\n";
	static const char invalid_screen[] =
		"func main() { Screen.put(25, 0, \"bad\", 225); }";
	static const char invalid_directory[] =
		"func main() { Directory.list(\"/SUB\"); }";
	static const char missing_import[] =
		"func main() { System.import(\"MISSING.NCT\"); }";
	static const char file_script[] =
		"func main() { FileUtil.writeText(\"/M10.TXT\", \"alpha\"); "
		"var f = File.open(\"/M10.TXT\", \"r\"); File.seek(f, 2); "
		"var p = File.tell(f); File.close(f); "
		"Console.write(FileUtil.readText(\"/M10.TXT\")); return p; }";
	static const char finalizer_script[] =
		"func main() { var f = File.open(\"/FINAL.TXT\", \"w\"); "
		"return 0; }";
	char *script_arguments[] = { "alpha", "beta" };
	char *ls_bad_arguments[] = { "one", "two" };
	char *cp_arguments[] = { "/SOURCE.BIN", "/COPY.BIN" };
	char *cp_same_arguments[] = { "/SOURCE.BIN", "source.bin" };
	char *cp_missing_arguments[] = { "/MISSING.BIN", "/COPY.BIN" };
	static char interpreter_output[OUTPUT_SIZE];
	static const char ls_output[] =
		"BOOT.CFG 7\nSCRIPTS/ 0\nLIB.NCT 38\n";
	static const char cp_output[] = "Copied 16417 bytes.\n";
	struct boot98_noct_result result;
	const struct boot98_filesystem_driver *drivers[] = { &memory_driver };
	struct boot98_volume volume = {
		.sector_size = 512,
		.read = volume_read,
	};
	struct boot98_filesystem filesystem;
	unsigned iteration;
	int status;

	if (argc != 4 ||
	    !read_host_source(argv[2], ls_source, sizeof(ls_source)) ||
	    !read_host_source(argv[3], cp_source, sizeof(cp_source)) ||
	    host_syscall3(SYS_MPROTECT, (long)(uintptr_t)arena,
			  sizeof(arena), 7) != 0)
		return 1;
	status = run_case(BOOT98_NOCT_M6_SOURCE, 0, BOOT98_NOCT_OK,
			  BOOT98_NOCT_M6_OUTPUT, &result);
	if (status != 0)
		return status;
	memcpy(interpreter_output, output, output_length + 1U);
	for (iteration = 0; iteration < 100U; iteration++) {
		status = run_case(BOOT98_NOCT_M6_SOURCE, 1, BOOT98_NOCT_OK,
				  BOOT98_NOCT_M6_OUTPUT, &result);
		if (status != 0 || strcmp(output, interpreter_output) != 0)
			return status != 0 ? 40 + status : 79;
	}
	status = run_case(syntax_error, 0, BOOT98_NOCT_SOURCE_ERROR, NULL,
			  &result);
	if (status != 0)
		return 80 + status;
	status = run_case(runtime_error, 0, BOOT98_NOCT_RUNTIME_ERROR, NULL,
			  &result);
	if (status != 0)
		return 120 + status;
	status = run_case_args(argument_script, 2, script_arguments, 0,
			       BOOT98_NOCT_OK, 7, "[alpha][beta]", &result);
	if (status != 0)
		return 160 + status;
	status = run_case_args(zero_argument_script, 2, script_arguments, 0,
			       BOOT98_NOCT_OK, 0, "zero", &result);
	if (status != 0)
		return 170 + status;
	status = run_case_args(long_status_script, 0, NULL, 0,
			       BOOT98_NOCT_OK, 9, "long", &result);
	if (status != 0)
		return 175 + status;
	status = run_case(bad_signature, 0, BOOT98_NOCT_SIGNATURE_ERROR,
			  NULL, &result);
	if (status != 0)
		return 180 + status;
	if (boot98_key_normalize_bios_ax(0x1c0d) != BOOT98_KEY_ENTER ||
	    boot98_key_normalize_bios_ax(0x3b00) != BOOT98_KEY_LEFT ||
	    boot98_key_normalize_bios_ax(0x3900) != BOOT98_KEY_DELETE ||
	    boot98_key_normalize_bios_ax(0xff00) != 0x1ff)
		return 190;
	memset(&mock, 0, sizeof(mock));
	status = run_case(napi_script, 0, BOOT98_NOCT_OK, napi_output, &result);
	if (status != 0)
		return 200 + status;
	if (mock.clear_count != 1 || mock.clear_row != 7 ||
	    mock.put_row != 2 || mock.put_column != 3 ||
	    mock.put_attribute != 225 || strcmp(mock.put_text, "AB") != 0 ||
	    mock.cursor_row != 4 || mock.cursor_column != 5 ||
	    mock.cursor_visible != 0)
		return 240;
	for (iteration = 0; iteration < 20U; iteration++) {
		status = run_case_args(ls_source, 0, NULL, 1, BOOT98_NOCT_OK,
				       0, ls_output, &result);
		if (status != 0)
			return 50 + status;
	}
	status = run_case_args(ls_source, 2, ls_bad_arguments, 0,
			       BOOT98_NOCT_OK, 2, "usage: ls [PATH]\n",
			       &result);
	if (status != 0)
		return 60 + status;
	status = run_case(invalid_screen, 0, BOOT98_NOCT_RUNTIME_ERROR, NULL,
			  &result);
	if (status != 0)
		return 245 + status;
	status = run_case(invalid_directory, 0, BOOT98_NOCT_RUNTIME_ERROR, NULL,
			  &result);
	if (status != 0)
		return 250 + status;
	status = run_case(missing_import, 0, BOOT98_NOCT_RUNTIME_ERROR, NULL,
			  &result);
	if (status != 0)
		return 255 + status;
	memset(records, 0, sizeof(records));
	if (!boot98_fs_mount(&filesystem, &volume, drivers, 1))
		return 258;
	test_filesystem = &filesystem;
	status = run_case_args(file_script, 0, NULL, 0, BOOT98_NOCT_OK, 2,
			       "alpha", &result);
	if (status != 0 || !records[0].exists || records[0].size != 5 ||
	    memcmp(records[0].data, "alpha", 5) != 0 || records[0].flushes == 0)
		return 260 + status;
	status = run_case(finalizer_script, 0, BOOT98_NOCT_OK, "", &result);
	if (status != 0 || !records[1].exists || records[1].flushes == 0)
		return 300 + status;
	memset(records, 0, sizeof(records));
	strcpy(records[0].name, "SOURCE.BIN");
	records[0].exists = 1;
	records[0].size = 16417;
	for (iteration = 0; iteration < records[0].size; iteration++)
		records[0].data[iteration] = (unsigned char)(iteration * 37U + 11U);
	for (iteration = 0; iteration < 20U; iteration++) {
		status = run_case_args(cp_source, 2, cp_arguments, 1,
				       BOOT98_NOCT_OK, 0, cp_output, &result);
		if (status != 0)
			return 70 + status;
		if (!records[1].exists ||
		    records[1].size != records[0].size ||
		    memcmp(records[1].data, records[0].data,
			   (size_t)records[0].size) != 0 ||
		    records[1].flushes == 0)
			return 80;
	}
	status = run_case_args(cp_source, 2, cp_same_arguments, 1,
			       BOOT98_NOCT_OK, 2,
			       "cp: source and destination are the same file\n",
			       &result);
	if (status != 0)
		return 90 + status;
	status = run_case_args(cp_source, 2, cp_missing_arguments, 1,
			       BOOT98_NOCT_OK, 1,
			       "cp: source file not found: /MISSING.BIN\n",
			       &result);
	if (status != 0)
		return 100 + status;
	test_filesystem = NULL;
	if (!write_capture_file(argv[1]))
		return 340;
	return 0;
}
