/*
 * PC-98 Bootstrap Environment Noct native APIs
 * Copyright (C) 2026 Awe Morris
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "boot98-noct-napi.h"
#include "boot98-beui.h"
#include "boot98-beui-image.h"
#include "boot98-noct.h"
#include "boot98-noct-memory.h"
#include "boot98-env.h"
#include "libc/boot98-heap.h"

#include <noct/noct.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SERIALIZE_LIMIT 4096U
#define SERIALIZE_DEPTH 4U
#define SERIALIZE_ITEMS 64U
#define BEUI_IMAGE_SOURCE_MAX (2U * 1024U * 1024U)
#define BEUI_IMAGE_PIXELS_MAX (2U * 1024U * 1024U)

struct imported_source {
	struct imported_source *next;
	char *path;
	char *source;
};

struct beui_image_handle {
	struct beui_image_handle *next;
	int identifier;
	struct boot98_beui_image image;
	uint8_t pixels[];
};

struct active_napi {
	const struct boot98_noct_services *services;
	boot98_noct_write_fn write;
	void *write_context;
	size_t arena_size;
	size_t source_max;
	struct imported_source *imports;
	struct beui_image_handle *images;
	int next_image_identifier;
	struct boot98_environment *environment;
};

struct api_item {
	const char *global_name;
	const char *field_name;
	size_t parameter_count;
	const char *parameters[6];
	bool (*function)(NoctEnv *env);
};

struct serializer {
	NoctEnv *env;
	size_t bytes;
};

static struct active_napi active;

static int services_ready(void);

static void
write_bytes(const char *bytes, size_t length)
{
	if (active.write != NULL && length != 0)
		(void)active.write(active.write_context, bytes, length);
}

static void
write_string(const char *string)
{
	if (string != NULL)
		write_bytes(string, strlen(string));
}

static bool
return_int(NoctEnv *env, int value)
{
	NoctValue result;
	bool ok;

	memset(&result, 0, sizeof(result));
	if (!noct_pin_local(env, 1, &result))
		return false;
	ok = noct_set_return_make_int(env, &result, value);
	(void)noct_unpin_local(env, 1, &result);
	return ok;
}

static bool
return_string(NoctEnv *env, const char *value)
{
	NoctValue result;
	bool ok;

	memset(&result, 0, sizeof(result));
	if (!noct_pin_local(env, 1, &result))
		return false;
	ok = noct_set_return_make_string(env, &result, value);
	(void)noct_unpin_local(env, 1, &result);
	return ok;
}

static bool
register_module(NoctEnv *env, const char *module,
		struct api_item *items, size_t item_count)
{
	NoctValue dictionary;
	NoctValue function;
	size_t index;
	bool ok = false;

	memset(&dictionary, 0, sizeof(dictionary));
	memset(&function, 0, sizeof(function));
	if (!noct_pin_local(env, 2, &dictionary, &function))
		return false;
	if (!noct_make_empty_dict(env, &dictionary) ||
	    !noct_set_global(env, module, &dictionary))
		goto out;
	for (index = 0; index < item_count; index++) {
		struct api_item *item = &items[index];

		if (!noct_register_cfunc(env, item->global_name,
					 item->parameter_count,
					 item->parameters, item->function, NULL) ||
		    !noct_get_global(env, item->global_name, &function) ||
		    !noct_set_dict_elem_cstr(env, &dictionary,
					     item->field_name, &function))
			goto out;
	}
	ok = true;
out:
	(void)noct_unpin_local(env, 2, &dictionary, &function);
	return ok;
}

/* Intrinsic-like global conveniences are declared in one auditable table. */
static bool
register_intrinsics(NoctEnv *env, struct api_item *items,
		    size_t item_count)
{
	size_t index;

	for (index = 0; index < item_count; index++) {
		struct api_item *item = &items[index];

		if (!noct_register_cfunc(env, item->global_name,
					 item->parameter_count, item->parameters,
					 item->function, NULL))
			return false;
	}
	return true;
}

static void
serialize_emit(struct serializer *output, const char *bytes, size_t length)
{
	size_t remaining;

	if (output->bytes >= SERIALIZE_LIMIT)
		return;
	remaining = SERIALIZE_LIMIT - output->bytes;
	if (length > remaining)
		length = remaining;
	write_bytes(bytes, length);
	output->bytes += length;
}

static void
serialize_string(struct serializer *output, const char *string)
{
	serialize_emit(output, string, strlen(string));
}

static bool serialize_value(struct serializer *output, NoctValue *value,
			    unsigned depth, bool quoted);

static bool
serialize_array(struct serializer *output, NoctValue *value, unsigned depth)
{
	NoctValue element;
	size_t count;
	size_t index;

	memset(&element, 0, sizeof(element));
	if (!noct_get_array_size(output->env, value, &count))
		return false;
	serialize_string(output, "[");
	if (depth >= SERIALIZE_DEPTH) {
		serialize_string(output, "...]");
		return true;
	}
	for (index = 0; index < count && index < SERIALIZE_ITEMS; index++) {
		if (index != 0)
			serialize_string(output, ", ");
		if (!noct_get_array_elem(output->env, value, index, &element) ||
		    !serialize_value(output, &element, depth + 1U, true))
			return false;
	}
	if (count > SERIALIZE_ITEMS)
		serialize_string(output, ", ...");
	serialize_string(output, "]");
	return true;
}

static bool
serialize_dict(struct serializer *output, NoctValue *value, unsigned depth)
{
	NoctValue key;
	NoctValue element;
	size_t count;
	size_t index;
	const char *name;

	memset(&key, 0, sizeof(key));
	memset(&element, 0, sizeof(element));
	if (!noct_get_dict_size(output->env, value, &count))
		return false;
	serialize_string(output, "{");
	if (depth >= SERIALIZE_DEPTH) {
		serialize_string(output, "...}");
		return true;
	}
	for (index = 0; index < count && index < SERIALIZE_ITEMS; index++) {
		if (index != 0)
			serialize_string(output, ", ");
		if (!noct_get_dict_by_index(output->env, value, index,
					    &key, &element) ||
		    !noct_get_string(output->env, &key, &name))
			return false;
		serialize_string(output, name);
		serialize_string(output, ": ");
		if (!serialize_value(output, &element, depth + 1U, true))
			return false;
	}
	if (count > SERIALIZE_ITEMS)
		serialize_string(output, ", ...");
	serialize_string(output, "}");
	return true;
}

static bool
serialize_value(struct serializer *output, NoctValue *value, unsigned depth,
		bool quoted)
{
	char buffer[64];
	const char *string;
	int type;

	if (!noct_get_value_type(output->env, value, &type))
		return false;
	switch (type) {
	case NOCT_VALUE_INT: {
		int number;
		if (!noct_get_int(output->env, value, &number))
			return false;
		(void)snprintf(buffer, sizeof(buffer), "%d", number);
		serialize_string(output, buffer);
		return true;
	}
	case NOCT_VALUE_LONG: {
		int64_t number;
		if (!noct_get_long(output->env, value, &number))
			return false;
		(void)snprintf(buffer, sizeof(buffer), "%" PRId64, number);
		serialize_string(output, buffer);
		return true;
	}
	case NOCT_VALUE_FLOAT: {
		float number;
		if (!noct_get_float(output->env, value, &number))
			return false;
		(void)snprintf(buffer, sizeof(buffer), "%.7g", (double)number);
		serialize_string(output, buffer);
		return true;
	}
	case NOCT_VALUE_DOUBLE: {
		double number;
		if (!noct_get_double(output->env, value, &number))
			return false;
		(void)snprintf(buffer, sizeof(buffer), "%.15g", number);
		serialize_string(output, buffer);
		return true;
	}
	case NOCT_VALUE_STRING:
		if (!noct_get_string(output->env, value, &string))
			return false;
		if (quoted)
			serialize_string(output, "\"");
		serialize_string(output, string);
		if (quoted)
			serialize_string(output, "\"");
		return true;
	case NOCT_VALUE_ARRAY:
		return serialize_array(output, value, depth);
	case NOCT_VALUE_DICT:
		return serialize_dict(output, value, depth);
	case NOCT_VALUE_FUNC:
		serialize_string(output, "<func>");
		return true;
	case NOCT_VALUE_PACKED:
		serialize_string(output, "<packed>");
		return true;
	default:
		serialize_string(output, "<unknown>");
		return true;
	}
}

static bool
cfunc_console_write(NoctEnv *env)
{
	NoctValue value;
	const char *text;
	bool ok = false;

	memset(&value, 0, sizeof(value));
	if (!noct_pin_local(env, 1, &value))
		return false;
	if (noct_get_arg(env, 0, &value) &&
	    noct_get_string(env, &value, &text)) {
		write_string(text);
		ok = true;
	}
	(void)noct_unpin_local(env, 1, &value);
	return ok;
}

static bool
cfunc_console_print(NoctEnv *env)
{
	NoctValue value;
	struct serializer output;
	bool ok = false;

	memset(&value, 0, sizeof(value));
	output.env = env;
	output.bytes = 0;
	if (!noct_pin_local(env, 1, &value))
		return false;
	if (noct_get_arg(env, 0, &value) &&
	    serialize_value(&output, &value, 0, false)) {
		write_string("\n");
		ok = true;
	}
	(void)noct_unpin_local(env, 1, &value);
	return ok;
}

/* Bounded, ASCII line input.  Unlike C gets(), this can never overflow. */
static bool
cfunc_console_gets(NoctEnv *env)
{
	char line[256];
	size_t length = 0;

	if (!services_ready() || active.services->keyboard_read == NULL) {
		noct_error(env, "gets is unavailable.");
		return false;
	}
	if (active.services->screen_show_cursor != NULL)
		(void)active.services->screen_show_cursor(
			active.services->context, 1);
	for (;;) {
		int key = active.services->keyboard_read(active.services->context);

		if (key < 0) {
			noct_error(env, "gets failed.");
			return false;
		}
		if (key > 0xff)
			continue;
		if (key == '\r' || key == '\n') {
			write_string("\n");
			line[length] = '\0';
			return return_string(env, line);
		}
		if (key == 0x03) {
			write_string("^C\n");
			return return_string(env, "");
		}
		if (key == '\b' || key == 0x7f) {
			if (length != 0) {
				length--;
				write_string("\b");
			}
			continue;
		}
		if (key >= 32 && key < 127 && length + 1U < sizeof(line)) {
			line[length++] = (char)key;
			write_bytes((const char *)&line[length - 1U], 1U);
		}
	}
}

static int
services_ready(void)
{
	return active.services != NULL;
}

static bool
cfunc_screen_get_width(NoctEnv *env)
{
	return return_int(env, 80);
}

static bool
cfunc_screen_get_height(NoctEnv *env)
{
	return return_int(env, 25);
}

static bool
cfunc_screen_clear(NoctEnv *env)
{
	if (!services_ready() || active.services->screen_clear == NULL ||
	    !active.services->screen_clear(active.services->context)) {
		noct_error(env, "Screen.clear is unavailable.");
		return false;
	}
	return return_int(env, 0);
}

static bool
cfunc_screen_clear_row(NoctEnv *env)
{
	NoctValue argument;
	int row;
	bool ok = false;

	memset(&argument, 0, sizeof(argument));
	if (!noct_pin_local(env, 1, &argument))
		return false;
	if (!noct_get_arg_check_int(env, 0, &argument, &row) || row < 0 ||
	    row >= 25 || !services_ready() ||
	    active.services->screen_clear_row == NULL ||
	    !active.services->screen_clear_row(active.services->context,
					       (unsigned)row))
		noct_error(env, "Screen.clearRow received an invalid row.");
	else
		ok = return_int(env, 0);
	(void)noct_unpin_local(env, 1, &argument);
	return ok;
}

static bool
cfunc_screen_put(NoctEnv *env)
{
	NoctValue argument;
	int row;
	int column;
	int attribute;
	int cells;
	const char *text;
	bool ok = false;

	memset(&argument, 0, sizeof(argument));
	if (!noct_pin_local(env, 1, &argument))
		return false;
	if (!noct_get_arg_check_int(env, 0, &argument, &row) ||
	    !noct_get_arg_check_int(env, 1, &argument, &column) ||
	    !noct_get_arg_check_string(env, 2, &argument, &text) ||
	    !noct_get_arg_check_int(env, 3, &argument, &attribute) ||
	    row < 0 || row >= 25 || column < 0 || column >= 80 ||
	    attribute < 0 || attribute > 255 || !services_ready() ||
	    active.services->screen_put == NULL) {
		noct_error(env, "Screen.put received an invalid argument.");
		goto out;
	}
	cells = active.services->screen_put(active.services->context,
					    (unsigned)row, (unsigned)column,
					    text, (uint8_t)attribute);
	if (cells < 0) {
		noct_error(env, "Screen.put failed.");
		goto out;
	}
	ok = return_int(env, cells);
out:
	(void)noct_unpin_local(env, 1, &argument);
	return ok;
}

static bool
cfunc_screen_set_cursor(NoctEnv *env)
{
	NoctValue argument;
	int row;
	int column;
	bool ok = false;

	memset(&argument, 0, sizeof(argument));
	if (!noct_pin_local(env, 1, &argument))
		return false;
	if (!noct_get_arg_check_int(env, 0, &argument, &row) ||
	    !noct_get_arg_check_int(env, 1, &argument, &column) ||
	    row < 0 || row >= 25 || column < 0 || column >= 80 ||
	    !services_ready() || active.services->screen_set_cursor == NULL ||
	    !active.services->screen_set_cursor(active.services->context,
						(unsigned)row,
						(unsigned)column))
		noct_error(env, "Screen.setCursor received an invalid position.");
	else
		ok = return_int(env, 0);
	(void)noct_unpin_local(env, 1, &argument);
	return ok;
}

static bool
cfunc_screen_show_cursor(NoctEnv *env)
{
	NoctValue argument;
	int visible;
	bool ok = false;

	memset(&argument, 0, sizeof(argument));
	if (!noct_pin_local(env, 1, &argument))
		return false;
	if (!noct_get_arg_check_int(env, 0, &argument, &visible) ||
	    !services_ready() || active.services->screen_show_cursor == NULL ||
	    !active.services->screen_show_cursor(active.services->context,
						  visible != 0))
		noct_error(env, "Screen.showCursor failed.");
	else
		ok = return_int(env, 0);
	(void)noct_unpin_local(env, 1, &argument);
	return ok;
}

static bool
cfunc_keyboard_poll(NoctEnv *env)
{
	int key;

	if (!services_ready() || active.services->keyboard_poll == NULL) {
		noct_error(env, "Keyboard.poll is unavailable.");
		return false;
	}
	key = active.services->keyboard_poll(active.services->context);
	if (key >= 0)
		key = boot98_key_normalize_bios_ax((uint16_t)key);
	return return_int(env, key);
}

static bool
cfunc_keyboard_read(NoctEnv *env)
{
	int key;

	if (!services_ready() || active.services->keyboard_read == NULL) {
		noct_error(env, "Keyboard.read is unavailable.");
		return false;
	}
	key = active.services->keyboard_read(active.services->context);
	if (key >= 0)
		key = boot98_key_normalize_bios_ax((uint16_t)key);
	return return_int(env, key);
}

static bool
cfunc_keyboard_is_printable(NoctEnv *env)
{
	NoctValue argument;
	int key;
	bool ok = false;

	memset(&argument, 0, sizeof(argument));
	if (!noct_pin_local(env, 1, &argument))
		return false;
	if (!noct_get_arg_check_int(env, 0, &argument, &key))
		goto out;
	ok = return_int(env, (key >= 0x20 && key <= 0x7e) ||
			     (key >= 0xa1 && key <= 0xdf));
out:
	(void)noct_unpin_local(env, 1, &argument);
	return ok;
}

static bool
cfunc_beui_init(NoctEnv *env)
{
	if (!boot98_beui_init()) {
		noct_error(env, "BeUI.init is unavailable.");
		return false;
	}
	return return_int(env, 1);
}

static bool
cfunc_beui_close(NoctEnv *env)
{
	boot98_beui_close();
	return return_int(env, 1);
}

static bool
cfunc_beui_is_open(NoctEnv *env)
{
	return return_int(env, boot98_beui_is_open());
}

static bool
cfunc_beui_get_width(NoctEnv *env)
{
	struct boot98_beui_display_info info;

	if (!boot98_beui_get_display_info(&info)) {
		noct_error(env, "BeUI is not initialized.");
		return false;
	}
	return return_int(env, (int)info.width);
}

static bool
cfunc_beui_get_height(NoctEnv *env)
{
	struct boot98_beui_display_info info;

	if (!boot98_beui_get_display_info(&info)) {
		noct_error(env, "BeUI is not initialized.");
		return false;
	}
	return return_int(env, (int)info.height);
}

static bool
cfunc_beui_poll(NoctEnv *env)
{
	if (!boot98_beui_poll()) {
		noct_error(env, "BeUI.poll failed.");
		return false;
	}
	return return_int(env, 1);
}

static bool
cfunc_beui_flush(NoctEnv *env)
{
	if (!boot98_beui_flush()) {
		noct_error(env, "BeUI.flush failed.");
		return false;
	}
	return return_int(env, 1);
}

static struct beui_image_handle *
find_beui_image(int identifier)
{
	struct beui_image_handle *handle;

	for (handle = active.images; handle != NULL; handle = handle->next)
		if (handle->identifier == identifier)
			return handle;
	return NULL;
}

static bool
cfunc_beui_text_width(NoctEnv *env)
{
	NoctValue argument;
	const char *text;
	unsigned width;
	unsigned height;
	bool ok = false;

	memset(&argument, 0, sizeof(argument));
	if (!noct_pin_local(env, 1, &argument))
		return false;
	if (!noct_get_arg_check_string(env, 0, &argument, &text) ||
	    !boot98_beui_measure_text(text, &width, &height)) {
		noct_error(env, "BeUI.textWidth failed.");
		goto out;
	}
	(void)height;
	ok = return_int(env, (int)width);
out:
	(void)noct_unpin_local(env, 1, &argument);
	return ok;
}

static bool
cfunc_beui_text_height(NoctEnv *env)
{
	NoctValue argument;
	const char *text;
	unsigned width;
	unsigned height;
	bool ok = false;

	memset(&argument, 0, sizeof(argument));
	if (!noct_pin_local(env, 1, &argument))
		return false;
	if (!noct_get_arg_check_string(env, 0, &argument, &text) ||
	    !boot98_beui_measure_text(text, &width, &height)) {
		noct_error(env, "BeUI.textHeight failed.");
		goto out;
	}
	(void)width;
	ok = return_int(env, (int)height);
out:
	(void)noct_unpin_local(env, 1, &argument);
	return ok;
}

static bool
cfunc_beui_draw_text(NoctEnv *env)
{
	NoctValue argument;
	const char *text;
	int x;
	int y;
	int foreground;
	int background;
	bool ok = false;

	memset(&argument, 0, sizeof(argument));
	if (!noct_pin_local(env, 1, &argument))
		return false;
	if (!noct_get_arg_check_string(env, 0, &argument, &text) ||
	    !noct_get_arg_check_int(env, 1, &argument, &x) ||
	    !noct_get_arg_check_int(env, 2, &argument, &y) ||
	    !noct_get_arg_check_int(env, 3, &argument, &foreground) ||
	    !noct_get_arg_check_int(env, 4, &argument, &background) ||
	    x < 0 || y < 0 || foreground < 0 || foreground > 0xffffff ||
	    background < 0 || background > 0xffffff ||
	    !boot98_beui_draw_text(text, (unsigned)x, (unsigned)y,
		(uint32_t)foreground, (uint32_t)background)) {
		noct_error(env, "BeUI.drawText received an invalid argument.");
		goto out;
	}
	ok = return_int(env, 1);
out:
	(void)noct_unpin_local(env, 1, &argument);
	return ok;
}

static bool
cfunc_beui_fill(NoctEnv *env)
{
	NoctValue argument;
	struct boot98_beui_rect rectangle;
	int x;
	int y;
	int width;
	int height;
	int color;
	bool ok = false;

	memset(&argument, 0, sizeof(argument));
	if (!noct_pin_local(env, 1, &argument))
		return false;
	if (!noct_get_arg_check_int(env, 0, &argument, &x) ||
	    !noct_get_arg_check_int(env, 1, &argument, &y) ||
	    !noct_get_arg_check_int(env, 2, &argument, &width) ||
	    !noct_get_arg_check_int(env, 3, &argument, &height) ||
	    !noct_get_arg_check_int(env, 4, &argument, &color) ||
	    x < 0 || y < 0 || width <= 0 || height <= 0 || color < 0 ||
	    color > 0xffffff) {
		noct_error(env, "BeUI.fill received an invalid argument.");
		goto out;
	}
	rectangle.x = (unsigned)x;
	rectangle.y = (unsigned)y;
	rectangle.width = (unsigned)width;
	rectangle.height = (unsigned)height;
	if (!boot98_beui_fill(&rectangle, (uint32_t)color)) {
		noct_error(env, "BeUI.fill failed.");
		goto out;
	}
	ok = return_int(env, 1);
out:
	(void)noct_unpin_local(env, 1, &argument);
	return ok;
}

static bool
cfunc_beui_line(NoctEnv *env)
{
	NoctValue argument;
	int x0;
	int y0;
	int x1;
	int y1;
	int color;
	bool ok = false;

	memset(&argument, 0, sizeof(argument));
	if (!noct_pin_local(env, 1, &argument))
		return false;
	if (!noct_get_arg_check_int(env, 0, &argument, &x0) ||
	    !noct_get_arg_check_int(env, 1, &argument, &y0) ||
	    !noct_get_arg_check_int(env, 2, &argument, &x1) ||
	    !noct_get_arg_check_int(env, 3, &argument, &y1) ||
	    !noct_get_arg_check_int(env, 4, &argument, &color) ||
	    x0 < 0 || y0 < 0 || x1 < 0 || y1 < 0 || color < 0 ||
	    color > 0xffffff ||
	    !boot98_beui_line((unsigned)x0, (unsigned)y0, (unsigned)x1,
			      (unsigned)y1, (uint32_t)color)) {
		noct_error(env, "BeUI.line received an invalid argument.");
		goto out;
	}
	ok = return_int(env, 1);
out:
	(void)noct_unpin_local(env, 1, &argument);
	return ok;
}

static bool
cfunc_beui_pattern_fill(NoctEnv *env)
{
	NoctValue argument;
	struct boot98_beui_rect rectangle;
	int x;
	int y;
	int width;
	int height;
	int color;
	int64_t pattern;
	bool ok = false;

	memset(&argument, 0, sizeof(argument));
	if (!noct_pin_local(env, 1, &argument))
		return false;
	if (!noct_get_arg_check_int(env, 0, &argument, &x) ||
	    !noct_get_arg_check_int(env, 1, &argument, &y) ||
	    !noct_get_arg_check_int(env, 2, &argument, &width) ||
	    !noct_get_arg_check_int(env, 3, &argument, &height) ||
	    !noct_get_arg_check_int(env, 4, &argument, &color) ||
	    !noct_get_arg_check_long(env, 5, &argument, &pattern) ||
	    x < 0 || y < 0 || width <= 0 || height <= 0 || color < 0 ||
	    color > 0xffffff) {
		noct_error(env, "BeUI.patternFill received an invalid argument.");
		goto out;
	}
	rectangle.x = (unsigned)x;
	rectangle.y = (unsigned)y;
	rectangle.width = (unsigned)width;
	rectangle.height = (unsigned)height;
	if (!boot98_beui_pattern_fill(&rectangle, (uint32_t)color,
				      (uint64_t)pattern)) {
		noct_error(env, "BeUI.patternFill failed.");
		goto out;
	}
	ok = return_int(env, 1);
out:
	(void)noct_unpin_local(env, 1, &argument);
	return ok;
}

static bool
cfunc_beui_load_image(NoctEnv *env)
{
	NoctValue argument;
	struct beui_image_handle *handle = NULL;
	enum boot98_beui_image_format format;
	const char *path;
	uint8_t *source = NULL;
	uint32_t source_size;
	size_t pixel_size;
	unsigned width;
	unsigned height;
	bool ok = false;

	memset(&argument, 0, sizeof(argument));
	if (!noct_pin_local(env, 1, &argument))
		return false;
	if (!noct_get_arg_check_string(env, 0, &argument, &path) ||
	    !services_ready() || active.services->file_size == NULL ||
	    active.services->file_read == NULL ||
	    !active.services->file_size(active.services->context, path,
					&source_size) ||
	    source_size == 0 || source_size > BEUI_IMAGE_SOURCE_MAX) {
		noct_error(env, "BeUI.loadImage cannot read the image.");
		goto out;
	}
	source = malloc(source_size);
	if (source == NULL ||
	    !active.services->file_read(active.services->context, path, 0,
				       source, source_size) ||
	    !boot98_beui_bmp_measure(source, source_size, &format, &width,
				     &height, &pixel_size) ||
	    pixel_size == 0 || pixel_size > BEUI_IMAGE_PIXELS_MAX ||
	    pixel_size > SIZE_MAX - sizeof(*handle)) {
		noct_error(env, "BeUI.loadImage received an unsupported BMP.");
		goto out;
	}
	(void)format;
	(void)width;
	(void)height;
	handle = malloc(sizeof(*handle) + pixel_size);
	if (handle == NULL || !boot98_beui_bmp_decode(source, source_size,
			handle->pixels, pixel_size, &handle->image)) {
		noct_error(env, "BeUI.loadImage has insufficient memory.");
		goto out;
	}
	if (active.next_image_identifier <= 0)
		active.next_image_identifier = 1;
	handle->identifier = active.next_image_identifier++;
	handle->next = active.images;
	active.images = handle;
	handle = NULL;
	ok = return_int(env, active.images->identifier);
out:
	free(handle);
	free(source);
	(void)noct_unpin_local(env, 1, &argument);
	return ok;
}

static bool
cfunc_beui_draw_image(NoctEnv *env)
{
	NoctValue argument;
	struct beui_image_handle *handle;
	int identifier;
	int x;
	int y;
	bool ok = false;

	memset(&argument, 0, sizeof(argument));
	if (!noct_pin_local(env, 1, &argument))
		return false;
	if (!noct_get_arg_check_int(env, 0, &argument, &identifier) ||
	    !noct_get_arg_check_int(env, 1, &argument, &x) ||
	    !noct_get_arg_check_int(env, 2, &argument, &y) || x < 0 || y < 0 ||
	    (handle = find_beui_image(identifier)) == NULL ||
	    !boot98_beui_draw_image((unsigned)x, (unsigned)y, &handle->image)) {
		noct_error(env, "BeUI.drawImage failed.");
		goto out;
	}
	ok = return_int(env, 1);
out:
	(void)noct_unpin_local(env, 1, &argument);
	return ok;
}

static bool
cfunc_beui_draw_image_pattern(NoctEnv *env)
{
	NoctValue argument;
	struct beui_image_handle *handle;
	int identifier;
	int x;
	int y;
	int64_t pattern;
	bool ok = false;

	memset(&argument, 0, sizeof(argument));
	if (!noct_pin_local(env, 1, &argument))
		return false;
	if (!noct_get_arg_check_int(env, 0, &argument, &identifier) ||
	    !noct_get_arg_check_int(env, 1, &argument, &x) ||
	    !noct_get_arg_check_int(env, 2, &argument, &y) ||
	    !noct_get_arg_check_long(env, 3, &argument, &pattern) ||
	    x < 0 || y < 0 ||
	    (handle = find_beui_image(identifier)) == NULL ||
	    !boot98_beui_draw_image_pattern((unsigned)x, (unsigned)y,
					    &handle->image,
					    (uint64_t)pattern)) {
		noct_error(env, "BeUI.drawImagePattern failed.");
		goto out;
	}
	ok = return_int(env, 1);
out:
	(void)noct_unpin_local(env, 1, &argument);
	return ok;
}

static bool
cfunc_beui_destroy_image(NoctEnv *env)
{
	NoctValue argument;
	struct beui_image_handle **link;
	int identifier;
	bool ok = false;

	memset(&argument, 0, sizeof(argument));
	if (!noct_pin_local(env, 1, &argument))
		return false;
	if (!noct_get_arg_check_int(env, 0, &argument, &identifier))
		goto invalid;
	for (link = &active.images; *link != NULL; link = &(*link)->next) {
		struct beui_image_handle *handle = *link;

		if (handle->identifier != identifier)
			continue;
		*link = handle->next;
		free(handle);
		ok = return_int(env, 1);
		goto out;
	}
invalid:
	noct_error(env, "BeUI.destroyImage received an invalid handle.");
out:
	(void)noct_unpin_local(env, 1, &argument);
	return ok;
}

static bool
make_directory_entry(NoctEnv *env, NoctValue *dictionary,
		     NoctValue *scratch, const struct boot98_noct_dirent *entry)
{
	return noct_make_empty_dict(env, dictionary) &&
	       noct_set_dict_elem_make_string(env, dictionary, "name", scratch,
					      entry->name) &&
	       noct_set_dict_elem_make_long(env, dictionary, "size", scratch,
					    (int64_t)entry->size) &&
	       noct_set_dict_elem_make_int(env, dictionary, "attributes", scratch,
					   entry->attributes) &&
	       noct_set_dict_elem_make_int(env, dictionary, "directory", scratch,
					   (entry->attributes & 0x10U) != 0);
}

static bool
cfunc_directory_list(NoctEnv *env)
{
	NoctValue argument;
	NoctValue array;
	NoctValue dictionary;
	NoctValue scratch;
	struct boot98_noct_dirent entry;
	const char *path;
	unsigned index;
	int status;
	bool ok = false;

	memset(&argument, 0, sizeof(argument));
	memset(&array, 0, sizeof(array));
	memset(&dictionary, 0, sizeof(dictionary));
	memset(&scratch, 0, sizeof(scratch));
	if (!noct_pin_local(env, 4, &argument, &array, &dictionary, &scratch))
		return false;
	if (!noct_get_arg_check_string(env, 0, &argument, &path) ||
	    !services_ready() || active.services->directory_read == NULL ||
	    !noct_make_empty_array(env, &array))
		goto error;
	for (index = 0; index < BOOT98_NOCT_DIRECTORY_MAX; index++) {
		status = active.services->directory_read(active.services->context,
							path, index, &entry);
		if (status < 0)
			goto error;
		if (status == 0)
			break;
		if (!make_directory_entry(env, &dictionary, &scratch, &entry) ||
		    !noct_set_array_elem(env, &array, index, &dictionary))
			goto error;
	}
	if (index == BOOT98_NOCT_DIRECTORY_MAX &&
	    active.services->directory_read(active.services->context, path,
					    index, &entry) != 0) {
		noct_error(env, "Directory contains too many entries.");
		goto out;
	}
	if (!noct_set_return(env, &array))
		goto error;
	ok = true;
	goto out;
error:
	noct_error(env, "Directory.list failed.");
out:
	(void)noct_unpin_local(env, 4, &argument, &array, &dictionary, &scratch);
	return ok;
}

static int
ascii_equal_folded(const char *left, const char *right)
{
	while (*left != '\0' && *right != '\0') {
		unsigned char a = (unsigned char)*left++;
		unsigned char b = (unsigned char)*right++;

		if (a >= 'a' && a <= 'z')
			a -= 'a' - 'A';
		if (b >= 'a' && b <= 'z')
			b -= 'a' - 'A';
		if (a != b)
			return 0;
	}
	return *left == *right;
}

static bool
cfunc_directory_stat(NoctEnv *env)
{
	NoctValue argument;
	NoctValue dictionary;
	NoctValue scratch;
	struct boot98_noct_dirent entry;
	const char *path;
	const char *name;
	unsigned index;
	int status;
	bool ok = false;

	memset(&argument, 0, sizeof(argument));
	memset(&dictionary, 0, sizeof(dictionary));
	memset(&scratch, 0, sizeof(scratch));
	if (!noct_pin_local(env, 3, &argument, &dictionary, &scratch))
		return false;
	if (!noct_get_arg_check_string(env, 0, &argument, &path) ||
	    !services_ready() || active.services->directory_read == NULL)
		goto error;
	if (strcmp(path, "") == 0 || strcmp(path, "/") == 0) {
		memset(&entry, 0, sizeof(entry));
		entry.name[0] = '/';
		entry.name[1] = '\0';
		entry.attributes = 0x10;
	} else {
		name = path[0] == '/' ? path + 1 : path;
		if (*name == '\0' || strchr(name, '/') != NULL)
			goto error;
		for (index = 0; index < BOOT98_NOCT_DIRECTORY_MAX; index++) {
			status = active.services->directory_read(
				active.services->context, "/", index, &entry);
			if (status <= 0)
				goto error;
			if (ascii_equal_folded(entry.name, name))
				break;
		}
		if (index == BOOT98_NOCT_DIRECTORY_MAX)
			goto error;
	}
	if (!make_directory_entry(env, &dictionary, &scratch, &entry) ||
	    !noct_set_return(env, &dictionary))
		goto error;
	ok = true;
	goto out;
error:
	noct_error(env, "Directory.stat failed.");
out:
	(void)noct_unpin_local(env, 3, &argument, &dictionary, &scratch);
	return ok;
}

static bool
cfunc_system_get_os_name(NoctEnv *env)
{
	return return_string(env, "PC98BE");
}

static bool
cfunc_system_get_env(NoctEnv *env)
{
	NoctValue argument;
	const char *name;
	const char *value;
	bool ok = false;

	memset(&argument, 0, sizeof(argument));
	if (!noct_pin_local(env, 1, &argument))
		return false;
	if (!noct_get_arg_check_string(env, 0, &argument, &name) ||
	    active.environment == NULL || !boot98_env_name_valid(name))
		goto error;
	value = boot98_env_get(active.environment, name);
	ok = return_string(env, value != NULL ? value : "");
	goto out;
error:
	noct_error(env, "System.getEnv received an invalid name.");
out:
	(void)noct_unpin_local(env, 1, &argument);
	return ok;
}

static bool
cfunc_system_set_env(NoctEnv *env)
{
	NoctValue name_value;
	NoctValue string_value;
	const char *name;
	const char *value;
	bool ok = false;

	memset(&name_value, 0, sizeof(name_value));
	memset(&string_value, 0, sizeof(string_value));
	if (!noct_pin_local(env, 2, &name_value, &string_value))
		return false;
	if (!noct_get_arg_check_string(env, 0, &name_value, &name) ||
	    !noct_get_arg_check_string(env, 1, &string_value, &value) ||
	    active.environment == NULL ||
	    !boot98_env_set(active.environment, name, value))
		goto error;
	ok = return_int(env, 0);
	goto out;
error:
	noct_error(env, "System.setEnv rejected the name, value, or full store.");
out:
	(void)noct_unpin_local(env, 2, &name_value, &string_value);
	return ok;
}

static bool
cfunc_system_unset_env(NoctEnv *env)
{
	NoctValue argument;
	const char *name;
	bool ok = false;

	memset(&argument, 0, sizeof(argument));
	if (!noct_pin_local(env, 1, &argument))
		return false;
	if (!noct_get_arg_check_string(env, 0, &argument, &name) ||
	    active.environment == NULL || !boot98_env_name_valid(name))
		goto error;
	(void)boot98_env_unset(active.environment, name);
	ok = return_int(env, 0);
	goto out;
error:
	noct_error(env, "System.unsetEnv received an invalid name.");
out:
	(void)noct_unpin_local(env, 1, &argument);
	return ok;
}

static bool
cfunc_system_list_env(NoctEnv *env)
{
	NoctValue dictionary;
	NoctValue scratch;
	size_t index;
	bool ok = false;

	memset(&dictionary, 0, sizeof(dictionary));
	memset(&scratch, 0, sizeof(scratch));
	if (active.environment == NULL ||
	    !noct_pin_local(env, 2, &dictionary, &scratch))
		return false;
	if (!noct_make_empty_dict(env, &dictionary))
		goto out;
	for (index = 0; index < boot98_env_count(active.environment); index++) {
		const char *name;
		const char *value;

		if (!boot98_env_at(active.environment, index, &name, &value) ||
		    !noct_set_dict_elem_make_string(env, &dictionary, name,
						 &scratch, value))
			goto out;
	}
	ok = noct_set_return(env, &dictionary);
out:
	(void)noct_unpin_local(env, 2, &dictionary, &scratch);
	return ok;
}

static bool
cfunc_system_memory_usage(NoctEnv *env)
{
	NoctValue dictionary;
	NoctValue scratch;
	bool ok = false;

	memset(&dictionary, 0, sizeof(dictionary));
	memset(&scratch, 0, sizeof(scratch));
	if (!noct_pin_local(env, 2, &dictionary, &scratch))
		return false;
	if (noct_make_empty_dict(env, &dictionary) &&
	    noct_set_dict_elem_make_long(env, &dictionary, "current", &scratch,
					 (int64_t)boot98_heap_current()) &&
	    noct_set_dict_elem_make_long(env, &dictionary, "peak", &scratch,
					 (int64_t)boot98_heap_peak()) &&
	    noct_set_dict_elem_make_long(env, &dictionary, "arenaSize", &scratch,
					 (int64_t)active.arena_size) &&
	    noct_set_return(env, &dictionary))
		ok = true;
	(void)noct_unpin_local(env, 2, &dictionary, &scratch);
	return ok;
}

static bool
cfunc_system_import(NoctEnv *env)
{
	NoctValue argument;
	struct imported_source *source = NULL;
	const char *path;
	size_t path_length;
	size_t allocation;
	uint32_t size;
	bool ok = false;

	memset(&argument, 0, sizeof(argument));
	if (!noct_pin_local(env, 1, &argument))
		return false;
	if (!noct_get_arg_check_string(env, 0, &argument, &path) ||
	    !services_ready() || active.services->file_size == NULL ||
	    active.services->file_read == NULL ||
	    !active.services->file_size(active.services->context, path, &size) ||
	    size > active.source_max)
		goto error;
	path_length = strlen(path);
	if (path_length >= BOOT98_NOCT_PATH_MAX ||
	    path_length > SIZE_MAX - sizeof(*source) - (size_t)size - 2U)
		goto error;
	allocation = sizeof(*source) + path_length + 1U + (size_t)size + 1U;
	source = malloc(allocation);
	if (source == NULL)
		goto error;
	source->path = (char *)(source + 1);
	source->source = source->path + path_length + 1U;
	memcpy(source->path, path, path_length + 1U);
	if ((size != 0 && !active.services->file_read(active.services->context,
						       path, 0, source->source,
						       size)) ||
	    (source->source[size] = '\0',
	     !noct_register_source(env, source->path, source->source)))
		goto error;
	source->next = active.imports;
	active.imports = source;
	source = NULL;
	ok = return_int(env, 0);
	goto out;
error:
	free(source);
	noct_error(env, "System.import failed.");
out:
	(void)noct_unpin_local(env, 1, &argument);
	return ok;
}

static bool
register_key_dictionary(NoctEnv *env)
{
	static const struct {
		const char *name;
		int value;
	} keys[] = {
		{ "Escape", BOOT98_KEY_ESCAPE },
		{ "Enter", BOOT98_KEY_ENTER },
		{ "Backspace", BOOT98_KEY_BACKSPACE },
		{ "Delete", BOOT98_KEY_DELETE },
		{ "Insert", BOOT98_KEY_INSERT },
		{ "Up", BOOT98_KEY_UP },
		{ "Down", BOOT98_KEY_DOWN },
		{ "Left", BOOT98_KEY_LEFT },
		{ "Right", BOOT98_KEY_RIGHT },
		{ "Home", BOOT98_KEY_HOME },
		{ "End", BOOT98_KEY_END },
		{ "PageUp", BOOT98_KEY_PAGE_UP },
		{ "PageDown", BOOT98_KEY_PAGE_DOWN },
		{ "F1", BOOT98_KEY_F1 }, { "F2", BOOT98_KEY_F2 },
		{ "F3", BOOT98_KEY_F3 }, { "F4", BOOT98_KEY_F4 },
		{ "F5", BOOT98_KEY_F5 }, { "F6", BOOT98_KEY_F6 },
		{ "F7", BOOT98_KEY_F7 }, { "F8", BOOT98_KEY_F8 },
		{ "F9", BOOT98_KEY_F9 }, { "F10", BOOT98_KEY_F10 },
	};
	NoctValue dictionary;
	NoctValue scratch;
	size_t index;
	bool ok = false;

	memset(&dictionary, 0, sizeof(dictionary));
	memset(&scratch, 0, sizeof(scratch));
	if (!noct_pin_local(env, 2, &dictionary, &scratch))
		return false;
	if (!noct_make_empty_dict(env, &dictionary))
		goto out;
	for (index = 0; index < sizeof(keys) / sizeof(keys[0]); index++)
		if (!noct_set_dict_elem_make_int(env, &dictionary,
						 keys[index].name, &scratch,
						 keys[index].value))
			goto out;
	if (!noct_set_global(env, "Key", &dictionary))
		goto out;
	ok = true;
out:
	(void)noct_unpin_local(env, 2, &dictionary, &scratch);
	return ok;
}

int
boot98_key_normalize_bios_ax(uint16_t bios_ax)
{
	unsigned ascii = bios_ax & 0xffU;

	if (ascii != 0)
		return (int)ascii;
	return 0x100 | (bios_ax >> 8);
}

int
boot98_noct_napi_register(NoctEnv *env,
			  const struct boot98_noct_options *options)
{
	static struct api_item console[] = {
		{ "Console.print", "print", 1, { "value" }, cfunc_console_print },
		{ "Console.write", "write", 1, { "text" }, cfunc_console_write },
		{ "Console.gets", "gets", 0, { NULL }, cfunc_console_gets },
	};
	static struct api_item intrinsics[] = {
		{ "print", NULL, 1, { "value" }, cfunc_console_print },
		{ "gets", NULL, 0, { NULL }, cfunc_console_gets },
	};
	static struct api_item screen[] = {
		{ "Screen.getWidth", "getWidth", 0, { NULL },
		  cfunc_screen_get_width },
		{ "Screen.getHeight", "getHeight", 0, { NULL },
		  cfunc_screen_get_height },
		{ "Screen.clear", "clear", 0, { NULL }, cfunc_screen_clear },
		{ "Screen.clearRow", "clearRow", 1, { "row" },
		  cfunc_screen_clear_row },
		{ "Screen.put", "put", 4,
		  { "row", "column", "text", "attribute" }, cfunc_screen_put },
		{ "Screen.setCursor", "setCursor", 2, { "row", "column" },
		  cfunc_screen_set_cursor },
		{ "Screen.showCursor", "showCursor", 1, { "visible" },
		  cfunc_screen_show_cursor },
	};
	static struct api_item keyboard[] = {
		{ "Keyboard.poll", "poll", 0, { NULL }, cfunc_keyboard_poll },
		{ "Keyboard.read", "read", 0, { NULL }, cfunc_keyboard_read },
		{ "Keyboard.isPrintable", "isPrintable", 1, { "code" },
		  cfunc_keyboard_is_printable },
	};
	static struct api_item beui[] = {
		{ "BeUI.init", "init", 0, { NULL }, cfunc_beui_init },
		{ "BeUI.close", "close", 0, { NULL }, cfunc_beui_close },
		{ "BeUI.isOpen", "isOpen", 0, { NULL }, cfunc_beui_is_open },
		{ "BeUI.getWidth", "getWidth", 0, { NULL },
		  cfunc_beui_get_width },
		{ "BeUI.getHeight", "getHeight", 0, { NULL },
		  cfunc_beui_get_height },
		{ "BeUI.poll", "poll", 0, { NULL }, cfunc_beui_poll },
		{ "BeUI.flush", "flush", 0, { NULL }, cfunc_beui_flush },
		{ "BeUI.fill", "fill", 5,
		  { "x", "y", "width", "height", "color" },
		  cfunc_beui_fill },
		{ "BeUI.line", "line", 5,
		  { "x0", "y0", "x1", "y1", "color" }, cfunc_beui_line },
		{ "BeUI.patternFill", "patternFill", 6,
		  { "x", "y", "width", "height", "color", "pattern" },
		  cfunc_beui_pattern_fill },
		{ "BeUI.textWidth", "textWidth", 1, { "text" },
		  cfunc_beui_text_width },
		{ "BeUI.textHeight", "textHeight", 1, { "text" },
		  cfunc_beui_text_height },
		{ "BeUI.drawText", "drawText", 5,
		  { "text", "x", "y", "foreground", "background" },
		  cfunc_beui_draw_text },
		{ "BeUI.loadImage", "loadImage", 1, { "path" },
		  cfunc_beui_load_image },
		{ "BeUI.drawImage", "drawImage", 3,
		  { "image", "x", "y" }, cfunc_beui_draw_image },
		{ "BeUI.drawImagePattern", "drawImagePattern", 4,
		  { "image", "x", "y", "pattern" },
		  cfunc_beui_draw_image_pattern },
		{ "BeUI.destroyImage", "destroyImage", 1, { "image" },
		  cfunc_beui_destroy_image },
	};
	static struct api_item directory[] = {
		{ "Directory.list", "list", 1, { "path" },
		  cfunc_directory_list },
		{ "Directory.stat", "stat", 1, { "path" },
		  cfunc_directory_stat },
	};
	static struct api_item system[] = {
		{ "System.getOSName", "getOSName", 0, { NULL },
		  cfunc_system_get_os_name },
		{ "System.import", "import", 1, { "path" },
		  cfunc_system_import },
		{ "System.memoryUsage", "memoryUsage", 0, { NULL },
		  cfunc_system_memory_usage },
		{ "System.getEnv", "getEnv", 1, { "name" },
		  cfunc_system_get_env },
		{ "System.setEnv", "setEnv", 2, { "name", "value" },
		  cfunc_system_set_env },
		{ "System.unsetEnv", "unsetEnv", 1, { "name" },
		  cfunc_system_unset_env },
		{ "System.listEnv", "listEnv", 0, { NULL },
		  cfunc_system_list_env },
	};

	if (env == NULL || options == NULL || options->write == NULL ||
	    active.write != NULL)
		return 0;
	active.services = options->services;
	active.write = options->write;
	active.write_context = options->write_context;
	active.arena_size = options->arena_size;
	active.source_max = options->memory != NULL ?
		options->memory->source_max : BOOT98_NOCT_SOURCE_MAX;
	active.imports = NULL;
	active.images = NULL;
	active.next_image_identifier = 1;
	active.environment = options->environment;
	if (!boot98_beui_bind(options->services != NULL ?
				options->services->beui : NULL) ||
	    !register_intrinsics(env, intrinsics,
				 sizeof(intrinsics) / sizeof(intrinsics[0])) ||
	    !register_module(env, "Console", console,
			     sizeof(console) / sizeof(console[0])) ||
	    !register_module(env, "Screen", screen,
			     sizeof(screen) / sizeof(screen[0])) ||
	    !register_module(env, "Keyboard", keyboard,
			     sizeof(keyboard) / sizeof(keyboard[0])) ||
	    !register_module(env, "BeUI", beui,
			     sizeof(beui) / sizeof(beui[0])) ||
	    !register_key_dictionary(env) ||
	    !register_module(env, "Directory", directory,
			     sizeof(directory) / sizeof(directory[0])) ||
	    !register_module(env, "System", system,
			     sizeof(system) / sizeof(system[0]))) {
		boot98_noct_napi_cleanup();
		return 0;
	}
	return 1;
}

void
boot98_noct_napi_cleanup(void)
{
	struct imported_source *source = active.imports;
	struct beui_image_handle *image = active.images;

	/* Restores text mode even when a script raises or omits BeUI.close(). */
	boot98_beui_cleanup();
	while (image != NULL) {
		struct beui_image_handle *next = image->next;

		free(image);
		image = next;
	}

	while (source != NULL) {
		struct imported_source *next = source->next;

		free(source);
		source = next;
	}
	memset(&active, 0, sizeof(active));
}
