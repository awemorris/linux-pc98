/*
 * PC-98 Bootstrap Environment Noct lifecycle and M8 NAPI test
 * Copyright (C) 2026 Awe Morris
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "boot98-noct.h"
#include "boot98-beui.h"
#include "boot98-noct-memory.h"
#include "boot98-noct-napi.h"
#include "boot98-noct-m6-script.h"
#include "boot98-env.h"
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
static const struct boot98_noct_memory_profile test_memory = {
	BOOT98_NOCT_MEMORY_5, "5M", 5, 0, ARENA_SIZE,
	64U * 1024U, 8U * 1024U,
	128U * 1024U, 32U * 1024U, 512U * 1024U, 96U * 1024U,
};
static unsigned char jit_capture[BOOT98_NOCT_JIT_CODE_MAX];
static size_t jit_capture_length;
static char output[OUTPUT_SIZE];
static size_t output_length;
static struct boot98_filesystem *test_filesystem;
static char ls_source[SCRIPT_SIZE];
static char cp_source[SCRIPT_SIZE];
static struct boot98_environment test_environment;

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
	int clear_column;
	int put_row;
	int put_column;
	int put_attribute;
	char put_text[32];
	int cursor_row;
	int cursor_column;
	int cursor_visible;
	const char *keyboard_input;
	size_t keyboard_position;
	int keyboard_bios_key;
	int keyboard_bios_queue[7];
	size_t keyboard_bios_count;
	size_t keyboard_bios_position;
	int beui_enter_count;
	int beui_leave_count;
	int beui_pointer_start_count;
	int beui_pointer_stop_count;
	int beui_pointer_poll_count;
	int beui_flush_count;
	int beui_fill_count;
	int beui_draw_count;
};

static struct mock_platform mock;
static const char imported_source[] =
	"func imported() { return \"imported\"; }";
static const uint8_t mock_bmp[] = {
	0x42, 0x4d, 0x3a, 0x00, 0x00, 0x00, 0, 0, 0, 0,
	0x36, 0x00, 0x00, 0x00, 0x28, 0x00, 0x00, 0x00,
	0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
	0x01, 0x00, 0x18, 0x00, 0, 0, 0, 0,
	0x04, 0x00, 0x00, 0x00, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0x00, 0x00, 0xff, 0x00,
};

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

static int mock_screen_put_utf8(void *context, unsigned row, unsigned column,
				const char *text, unsigned length,
				uint8_t attribute)
{
	struct mock_platform *platform = context;
	unsigned offset;
	int cells = 0;

	if (length >= sizeof(platform->put_text))
		return -1;
	platform->put_row = (int)row;
	platform->put_column = (int)column;
	platform->put_attribute = attribute;
	memcpy(platform->put_text, text, length);
	platform->put_text[length] = '\0';
	for (offset = 0; offset < length; offset++)
		if (((unsigned char)text[offset] & 0xc0U) != 0x80U)
			cells++;
	return cells;
}

static int mock_screen_clear_to_eol(void *context, unsigned row,
				    unsigned column)
{
	struct mock_platform *platform = context;

	platform->clear_row = (int)row;
	platform->clear_column = (int)column;
	return 1;
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
	struct mock_platform *platform = context;

	if (platform->keyboard_bios_position < platform->keyboard_bios_count)
		return platform->keyboard_bios_queue[
			platform->keyboard_bios_position];
	if (platform->keyboard_bios_key != 0)
		return platform->keyboard_bios_key;
	return 0x3b00; /* raw PC-98 BIOS AX for cursor left */
}

static int mock_keyboard_read(void *context)
{
	struct mock_platform *platform = context;
	int key;

	if (platform->keyboard_bios_position < platform->keyboard_bios_count)
		return platform->keyboard_bios_queue[
			platform->keyboard_bios_position++];
	if (platform->keyboard_bios_key != 0) {
		key = platform->keyboard_bios_key;
		platform->keyboard_bios_key = 0;
		return key;
	}

	if (platform->keyboard_input != NULL &&
	    platform->keyboard_input[platform->keyboard_position] != '\0')
		return (unsigned char)
			platform->keyboard_input[platform->keyboard_position++];
	return 'A';
}

static int mock_file_size(void *context, const char *path, uint32_t *size)
{
	(void)context;
	if (strcmp(path, "LIB.NCT") == 0)
		*size = (uint32_t)strlen(imported_source);
	else if (strcmp(path, "TEST.BMP") == 0)
		*size = sizeof(mock_bmp);
	else
		return 0;
	return 1;
}

static int mock_file_read(void *context, const char *path, uint32_t offset,
			  void *buffer, uint32_t length)
{
	(void)context;
	if (strcmp(path, "LIB.NCT") == 0) {
		if (offset > strlen(imported_source) ||
		    length > strlen(imported_source) - offset)
			return 0;
		memcpy(buffer, imported_source + offset, length);
	} else if (strcmp(path, "TEST.BMP") == 0) {
		if (offset > sizeof(mock_bmp) || length > sizeof(mock_bmp) - offset)
			return 0;
		memcpy(buffer, mock_bmp + offset, length);
	} else {
		return 0;
	}
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

static int
mock_beui_enter(void *context, struct boot98_beui_display_info *info)
{
	struct mock_platform *platform = context;

	platform->beui_enter_count++;
	info->width = 640;
	info->height = 400;
	info->bits_per_pixel = 4;
	info->stride = 80;
	return 1;
}

static void
mock_beui_leave(void *context)
{
	((struct mock_platform *)context)->beui_leave_count++;
}

static int
mock_beui_flush(void *context,
		 const struct boot98_beui_rect *rectangles,
		 size_t rectangle_count)
{
	struct mock_platform *platform = context;

	if (rectangles != NULL || rectangle_count != 0)
		return 0;
	platform->beui_flush_count++;
	return 1;
}

static int
mock_beui_fill(void *context, const struct boot98_beui_rect *rectangle,
	       uint32_t color)
{
	struct mock_platform *platform = context;

	if (rectangle->x != 1 || rectangle->y != 2 || rectangle->width != 3 ||
	    rectangle->height != 4 || color != 0x112233U)
		return 0;
	platform->beui_fill_count++;
	return 1;
}

static int
mock_beui_draw_image(void *context, unsigned x, unsigned y,
		     const struct boot98_beui_image *image)
{
	struct mock_platform *platform = context;

	if (x != 5 || y != 6 || image->format != BOOT98_BEUI_IMAGE_RGB24 ||
	    image->width != 1 || image->height != 1 || image->stride != 3 ||
	    image->pixels[0] != 0xff || image->pixels[1] != 0 ||
	    image->pixels[2] != 0)
		return 0;
	platform->beui_draw_count++;
	return 1;
}

static int
mock_beui_pointer_start(void *context,
			 const struct boot98_beui_display_info *display)
{
	struct mock_platform *platform = context;

	platform->beui_pointer_start_count++;
	return display->width == 640 && display->height == 400;
}

static void
mock_beui_pointer_stop(void *context)
{
	((struct mock_platform *)context)->beui_pointer_stop_count++;
}

static int
mock_beui_pointer_poll(void *context,
			struct boot98_beui_pointer_event *event)
{
	struct mock_platform *platform = context;

	platform->beui_pointer_poll_count++;
	memset(event, 0, sizeof(*event));
	return 0;
}

static const struct boot98_beui_hal mock_beui = {
	.display = {
		.context = &mock,
		.enter = mock_beui_enter,
		.leave = mock_beui_leave,
		.fill = mock_beui_fill,
		.draw_image = mock_beui_draw_image,
		.flush = mock_beui_flush,
	},
	.pointer = {
		.context = &mock,
		.start = mock_beui_pointer_start,
		.stop = mock_beui_pointer_stop,
		.poll = mock_beui_pointer_poll,
	},
};

static const struct boot98_noct_services mock_services = {
	.context = &mock,
	.beui = &mock_beui,
	.screen_clear = mock_screen_clear,
	.screen_clear_row = mock_screen_clear_row,
	.screen_put = mock_screen_put,
	.screen_put_utf8 = mock_screen_put_utf8,
	.screen_clear_to_eol = mock_screen_clear_to_eol,
	.screen_set_cursor = mock_screen_set_cursor,
	.screen_show_cursor = mock_screen_show_cursor,
	.keyboard_poll = mock_keyboard_poll,
	.keyboard_read = mock_keyboard_read,
	.file_size = mock_file_size,
	.file_read = mock_file_read,
	.directory_read = mock_directory_read,
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

struct repl_input {
	const char *const *lines;
	size_t line_count;
	size_t line_index;
	int continuation[16];
	size_t call_count;
};

static enum boot98_noct_repl_input_result
read_repl_input(void *context, int continuation, char *line, size_t capacity)
{
	struct repl_input *input = context;
	const char *source;
	size_t length;

	if (input->call_count <
	    sizeof(input->continuation) / sizeof(input->continuation[0]))
		input->continuation[input->call_count] = continuation;
	input->call_count++;
	if (input->line_index == input->line_count)
		return BOOT98_NOCT_REPL_INPUT_EXIT;
	source = input->lines[input->line_index++];
	length = strlen(source);
	if (length >= capacity)
		return BOOT98_NOCT_REPL_INPUT_ERROR;
	memcpy(line, source, length + 1U);
	return BOOT98_NOCT_REPL_INPUT_LINE;
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
	options.environment = &test_environment;
	options.memory = &test_memory;
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
		if (result.jit_code_size != test_memory.jit_code_size)
			return 32;
		if (!result.jit_region_released)
			return 34;
		if (jit_capture_length != test_memory.jit_code_size)
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

static int
run_repl_case(int jit_enable)
{
	static const char *const lines[] = {
		"func keep()",
		"{",
		"return \"M15_FUNCTION\";",
		"}",
		"Console.print(keep())",
		"var = 1",
		"Console.print(\"M15_RECOVERED\")",
		"System.setEnv(\"REPL\", \"yes\")",
	};
	static const int expected_continuation[] = {
		0, 1, 1, 1, 0, 0, 0, 0, 0,
	};
	struct boot98_noct_options options;
	struct boot98_noct_result result;
	struct repl_input input;
	size_t index;
	int success;

	memset(&input, 0, sizeof(input));
	input.lines = lines;
	input.line_count = sizeof(lines) / sizeof(lines[0]);
	output_length = 0;
	output[0] = '\0';
	jit_capture_length = 0;
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
	options.environment = &test_environment;
	options.memory = &test_memory;
	success = boot98_noct_repl(&options, read_repl_input, &input, &result);
	if (!success || result.status != BOOT98_NOCT_OK)
		return 1;
	if (input.line_index != input.line_count ||
	    input.call_count != sizeof(expected_continuation) /
				sizeof(expected_continuation[0]))
		return 2;
	for (index = 0; index < input.call_count; index++)
		if (input.continuation[index] != expected_continuation[index])
			return 3;
	if (strstr(output, "M15_FUNCTION\n") == NULL ||
	    strstr(output, "Noct REPL error:") == NULL ||
	    strstr(output, "M15_RECOVERED\n") == NULL)
		return 4;
	if (result.current_after_reset != 0 || boot98_heap_current() != 0 ||
	    result.heap_errors != 0 || !boot98_heap_validate())
		return 5;
	if (jit_enable) {
		if (result.jit_code_size != test_memory.jit_code_size ||
		    !result.jit_region_released ||
		    jit_capture_length != test_memory.jit_code_size)
			return 6;
	} else if (result.jit_code_size != 0 || result.jit_region_released) {
		return 7;
	}
	return 0;
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
	static const char intrinsic_script[] =
		"func main() { var line = gets(); print(line); return 0; }";
	static const char environment_set_script[] =
		"func main() { System.setEnv(\"MODE\", \"safe\"); "
		"print(System.getEnv(\"MODE\")); var all = System.listEnv(); "
		"print(all.MODE); return 0; }";
	static const char environment_persist_script[] =
		"func main() { print(System.getEnv(\"MODE\")); "
		"System.unsetEnv(\"MODE\"); print(System.getEnv(\"MODE\")); "
		"return 0; }";
	static const char term_script[] =
		"func main() { "
		"if (Term.isTTY() != 1 || Term.open() != 1) { return 1; } "
		"var size = Term.size(); "
		"if (size.rows != 25 || size.cols != 80) { return 2; } "
		"if (Term.setStyle({fg: 2, reverse: 1}) != 1 || "
		"Term.moveTo(2, 3) != 1 || "
		"Term.write(\"日本語\") != 1) { return 3; } "
		"if (Term.clearToEol() != 1 || Term.showCursor(1) != 1 || "
		"Term.flush() != 1) { return 4; } "
		"if (Term.readKey(10) != (Term.META | 0x78)) { return 5; } "
		"if (Term.readKey(10) != (Term.CTRL | 0x63)) { return 7; } "
		"if (Term.readKey(10) != (Term.CTRL | 0x20)) { return 9; } "
		"if (Term.readKey(10) != Term.KEY_LEFT) { return 8; } "
		"var entries = FileUtil.listDirectory(\"/\"); "
		"if (Array.size(entries) != 3 || entries[0] != \"BOOT.CFG\" || "
		"entries[1] != \"LIB.NCT\" || entries[2] != \"SCRIPTS/\") "
		"{ return 6; } Term.close(); return 0; }";
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
	static const char beui_script[] =
		"func main() { print(BeUI.isOpen()); print(BeUI.init()); "
		"print(BeUI.init()); print(BeUI.getWidth()); "
		"print(BeUI.getHeight()); print(BeUI.poll()); "
		"print(BeUI.fill(1, 2, 3, 4, 1122867)); "
		"var image = BeUI.loadImage(\"TEST.BMP\"); print(image); "
		"print(BeUI.drawImage(image, 5, 6)); "
		"print(BeUI.destroyImage(image)); "
		"print(BeUI.flush()); print(BeUI.close()); "
		"print(BeUI.isOpen()); return 0; }";
	static const char beui_cleanup_script[] =
		"func main() { BeUI.init(); return 0; }";
	static const char beui_error_script[] =
		"func main() { BeUI.init(); Console.write(1); }";
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
	boot98_env_init(&test_environment);
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
	status = run_repl_case(0);
	if (status != 0 ||
	    strcmp(boot98_env_get(&test_environment, "REPL"), "yes") != 0)
		return 185 + status;
	for (iteration = 0; iteration < 20U; iteration++) {
		status = run_repl_case(1);
		if (status != 0)
			return 190 + status;
	}
	if (!boot98_env_unset(&test_environment, "REPL"))
		return 191;
	if (boot98_key_normalize_bios_ax(0x1c0d) != BOOT98_KEY_ENTER ||
	    boot98_key_normalize_bios_ax(0x0f00) != BOOT98_KEY_TAB ||
	    boot98_key_normalize_bios_ax(0x0f09) != BOOT98_KEY_TAB ||
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
	for (iteration = 0; iteration < 2U; iteration++) {
		mock.keyboard_input = "OK\r";
		mock.keyboard_position = 0;
		status = run_case(intrinsic_script, iteration != 0,
				  BOOT98_NOCT_OK, "OK\nOK\n", &result);
		mock.keyboard_input = NULL;
		if (status != 0 || mock.cursor_visible != 1)
			return 242 + status;
	}
	status = run_case(environment_set_script, 0, BOOT98_NOCT_OK,
			  "safe\nsafe\n", &result);
	if (status != 0)
		return 244 + status;
	status = run_case(environment_persist_script, 0, BOOT98_NOCT_OK,
			  "safe\n\n", &result);
	if (status != 0 || boot98_env_get(&test_environment, "MODE") != NULL)
		return 246 + status;
	/* VM/API registration alone must never probe or enter graphics. */
	if (mock.beui_enter_count != 0 || mock.beui_pointer_start_count != 0)
		return 247;
	memset(&mock, 0, sizeof(mock));
	status = run_case(beui_script, 0, BOOT98_NOCT_OK,
			  "0\n1\n1\n640\n400\n1\n1\n1\n1\n1\n1\n1\n0\n", &result);
	if (status != 0 || mock.beui_enter_count != 1 ||
	    mock.beui_leave_count != 1 ||
	    mock.beui_pointer_start_count != 1 ||
	    mock.beui_pointer_stop_count != 1 ||
	    mock.beui_pointer_poll_count != 1 || mock.beui_flush_count != 1 ||
	    mock.beui_fill_count != 1 || mock.beui_draw_count != 1)
		return 248 + status;
	memset(&mock, 0, sizeof(mock));
	status = run_case(beui_cleanup_script, 1, BOOT98_NOCT_OK, "", &result);
	if (status != 0 || mock.beui_enter_count != 1 ||
	    mock.beui_leave_count != 1 ||
	    mock.beui_pointer_start_count != 1 ||
	    mock.beui_pointer_stop_count != 1)
		return 249 + status;
	memset(&mock, 0, sizeof(mock));
	status = run_case(beui_error_script, 0, BOOT98_NOCT_RUNTIME_ERROR,
			  NULL, &result);
	if (status != 0 || mock.beui_enter_count != 1 ||
	    mock.beui_leave_count != 1 ||
	    mock.beui_pointer_start_count != 1 ||
	    mock.beui_pointer_stop_count != 1)
		return 250 + status;
	memset(&mock, 0, sizeof(mock));
	mock.keyboard_bios_key = 0x3b00;
	/* Modifier-only BIOS make events must not reach Term.readKey(). */
	mock.keyboard_bios_queue[0] = 0x7000; /* Shift */
	mock.keyboard_bios_queue[1] = 0x7300; /* Graph (PC/AT Alt) */
	mock.keyboard_bios_queue[2] = 0x7400; /* Ctrl */
	mock.keyboard_bios_queue[3] = 0x001b;
	mock.keyboard_bios_queue[4] = 0x0078;
	mock.keyboard_bios_queue[5] = 0x0003;
	/* BL shift-state bit 4 is packed into bits 23:16 by Stage 1. */
	mock.keyboard_bios_queue[6] = 0x00100020;
	mock.keyboard_bios_count = 7;
	status = run_case_args(term_script, 0, NULL, 0, BOOT98_NOCT_OK, 0,
			       "", &result);
	if (status != 0)
		return 230 + status;
	if (mock.clear_count != 1)
		return 231;
	if (mock.put_row != 1 || mock.put_column != 2)
		return 232;
	if (strcmp(mock.put_text, "日本語") != 0)
		return 233;
	if (mock.put_attribute != 0x45)
		return 234;
	if (mock.clear_row != 1 || mock.clear_column != 5)
		return 235;
	if (mock.cursor_visible != 1)
		return 236;
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
