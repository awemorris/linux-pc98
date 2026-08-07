/* -*- coding: utf-8; tab-width: 8; indent-tabs-mode: t; -*- */

/*
 * Noct Programming Language
 * Copyright (c) 2025, 2026, Awe Morris
 */

/* API: File.* */

#include <noct/noct.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static bool cfunc_File_open(NoctEnv *env);
static bool cfunc_File_close(NoctEnv *env);
static bool cfunc_File_tell(NoctEnv *env);
static bool cfunc_File_seek(NoctEnv *env);
static bool cfunc_File_read(NoctEnv *env);
static bool cfunc_File_write(NoctEnv *env);
static void file_finalizer(void *native_pointer);
static bool cfunc_FileUtil_checkFileExists(NoctEnv *env);
static bool cfunc_FileUtil_getFileSize(NoctEnv *env);
static bool cfunc_FileUtil_readText(NoctEnv *env);
static bool cfunc_FileUtil_writeText(NoctEnv *env);
static bool cfunc_FileUtil_readForEachLine(NoctEnv *env);
static bool cfunc_FileUtil_writeForEachLine(NoctEnv *env);

struct ffi_item {
	const char *global_name;
	const char *package_name;
	const char *field_name;
	size_t param_count;
	const char *param[NOCT_ARG_MAX];
	bool (*cfunc)(NoctEnv *env);
};

static struct ffi_item ffi_items[] = {
	{"File.open", "File", "open", 2, {"path", "mode"}, cfunc_File_open},
	{"File.close", "File", "close", 1, {"file"}, cfunc_File_close},
	{"File.tell", "File", "tell", 1, {"file"}, cfunc_File_tell},
	{"File.seek", "File", "seek", 2, {"file", "offset"}, cfunc_File_seek},
	{"File.read", "File", "read", 2, {"file", "len"}, cfunc_File_read},
	{"File.write", "File", "write", 4,
	 {"file", "data", "offset", "size"}, cfunc_File_write},
	{"FileUtil.checkFileExists", "FileUtil", "checkFileExists", 1,
	 {"path"}, cfunc_FileUtil_checkFileExists},
	{"FileUtil.getFileSize", "FileUtil", "getFileSize", 1,
	 {"path"}, cfunc_FileUtil_getFileSize},
	{"FileUtil.readText", "FileUtil", "readText", 1,
	 {"path"}, cfunc_FileUtil_readText},
	{"FileUtil.writeText", "FileUtil", "writeText", 2,
	 {"path", "text"}, cfunc_FileUtil_writeText},
	{"FileUtil.readForEachLine", "FileUtil", "readForEachLine", 2,
	 {"path", "func"}, cfunc_FileUtil_readForEachLine},
	{"FileUtil.writeForEachLine", "FileUtil", "writeForEachLine", 2,
	 {"path", "lines"}, cfunc_FileUtil_writeForEachLine},
};

NOCT_DLL bool
noct_register_api_file(NoctEnv *env)
{
	NoctValue file_dict;
	NoctValue fileutil_dict;
	size_t i;

	if (!noct_make_empty_dict(env, &file_dict) ||
	    !noct_make_empty_dict(env, &fileutil_dict) ||
	    !noct_set_global(env, "File", &file_dict) ||
	    !noct_set_global(env, "FileUtil", &fileutil_dict))
		return false;
	for (i = 0; i < sizeof(ffi_items) / sizeof(ffi_items[0]); i++) {
		NoctValue funcval;
		NoctValue *package = !strcmp(ffi_items[i].package_name, "File") ?
			&file_dict : &fileutil_dict;

		if (!noct_register_cfunc(env, ffi_items[i].global_name,
					 ffi_items[i].param_count,
					 ffi_items[i].param,
					 ffi_items[i].cfunc, NULL) ||
		    !noct_get_global(env, ffi_items[i].global_name, &funcval) ||
		    !noct_set_dict_elem_cstr(env, package,
					     ffi_items[i].field_name, &funcval))
			return false;
	}
	return true;
}

static bool
get_file(NoctEnv *env, NoctValue *value, FILE **file)
{
	void (*finalizer)(void *);

	if (!noct_get_dict_native_pointer(env, value, (void **)file,
					  &finalizer))
		return false;
	if (*file == NULL) {
		noct_error(env, N_TR("File is closed."));
		return false;
	}
	return true;
}

static void
file_finalizer(void *native_pointer)
{
	if (native_pointer != NULL)
		(void)fclose((FILE *)native_pointer);
}

static bool
cfunc_File_open(NoctEnv *env)
{
	NoctValue path, mode, ret;
	const char *path_s, *mode_s;
	FILE *file = NULL;
	bool installed = false;
	bool ok = false;

	if (!noct_pin_local(env, 3, &path, &mode, &ret))
		return false;
	if (!noct_get_arg_check_string(env, 0, &path, &path_s) ||
	    !noct_get_arg_check_string(env, 1, &mode, &mode_s))
		goto cleanup;
	if (strcmp(mode_s, "r") && strcmp(mode_s, "rb") &&
	    strcmp(mode_s, "w") && strcmp(mode_s, "wb")) {
		noct_error(env, N_TR("Unsupported file mode."));
		goto cleanup;
	}
	file = fopen(path_s, mode_s);
	if (file == NULL) {
		noct_error(env, N_TR("Cannot open file %s."), path_s);
		goto cleanup;
	}
	if (!noct_make_empty_dict(env, &ret) ||
	    !noct_set_dict_native_pointer(env, &ret, file, file_finalizer))
		goto cleanup;
	installed = true;
	if (!noct_set_return(env, &ret))
		goto cleanup;
	ok = true;
cleanup:
	if (!ok && file != NULL) {
		/*
		 * Once installed, the dictionary finalizer owns the stream.  Clear
		 * that ownership before closing it ourselves.  If clearing fails,
		 * leave the stream to the finalizer instead of risking a double
		 * fclose on a native pointer that is still reachable by the VM.
		 */
		if (!installed ||
		    noct_set_dict_native_pointer(env, &ret, NULL, NULL))
			(void)fclose(file);
	}
	(void)noct_unpin_local(env, 3, &path, &mode, &ret);
	return ok;
}

static bool
cfunc_File_close(NoctEnv *env)
{
	NoctValue file_value, ret;
	FILE *file;
	bool ok = false;

	if (!noct_pin_local(env, 2, &file_value, &ret))
		return false;
	if (!noct_get_arg_check_dict(env, 0, &file_value) ||
	    !get_file(env, &file_value, &file))
		goto cleanup;
	/* Clear the finalizer before fclose frees the native stream. */
	if (!noct_set_dict_native_pointer(env, &file_value, NULL, NULL))
		goto cleanup;
	if (fclose(file) != 0) {
		noct_error(env, N_TR("File close error."));
		goto cleanup;
	}
	if (!noct_set_return_make_int(env, &ret, 0))
		goto cleanup;
	ok = true;
cleanup:
	(void)noct_unpin_local(env, 2, &file_value, &ret);
	return ok;
}

static bool
cfunc_File_tell(NoctEnv *env)
{
	NoctValue file_value, ret;
	FILE *file;
	long offset;
	bool ok = false;

	if (!noct_pin_local(env, 2, &file_value, &ret))
		return false;
	if (!noct_get_arg_check_dict(env, 0, &file_value) ||
	    !get_file(env, &file_value, &file))
		goto cleanup;
	offset = ftell(file);
	if (offset < 0) {
		noct_error(env, N_TR("File tell error."));
		goto cleanup;
	}
	if (!noct_set_return_make_int_long(env, &ret, (size_t)offset))
		goto cleanup;
	ok = true;
cleanup:
	(void)noct_unpin_local(env, 2, &file_value, &ret);
	return ok;
}

static bool
cfunc_File_seek(NoctEnv *env)
{
	NoctValue file_value, offset_value, ret;
	FILE *file;
	size_t offset;
	bool ok = false;

	if (!noct_pin_local(env, 3, &file_value, &offset_value, &ret))
		return false;
	if (!noct_get_arg_check_dict(env, 0, &file_value) ||
	    !noct_get_arg_check_int_long(env, 1, &offset_value, &offset) ||
	    !get_file(env, &file_value, &file))
		goto cleanup;
	if (offset > 0x7fffffffU || fseek(file, (long)offset, SEEK_SET) != 0) {
		noct_error(env, N_TR("File seek error."));
		goto cleanup;
	}
	if (!noct_set_return_make_int(env, &ret, 1))
		goto cleanup;
	ok = true;
cleanup:
	(void)noct_unpin_local(env, 3, &file_value, &offset_value, &ret);
	return ok;
}

static bool
cfunc_File_read(NoctEnv *env)
{
	NoctValue file_value, length_value, ret;
	FILE *file;
	size_t length, actual;
	void *buffer = NULL;
	bool transferred = false;
	bool ok = false;

	if (!noct_pin_local(env, 3, &file_value, &length_value, &ret))
		return false;
	if (!noct_get_arg_check_dict(env, 0, &file_value) ||
	    !noct_get_arg_check_int_long(env, 1, &length_value, &length) ||
	    !get_file(env, &file_value, &file))
		goto cleanup;
	/* Noct's packed representation currently requires at least one byte. */
	if (length == 0) {
		noct_error(env, N_TR("Read length must be greater than zero."));
		goto cleanup;
	}
	buffer = noct_malloc(length);
	if (buffer == NULL) {
		noct_error(env, N_TR("Out of memory."));
		goto cleanup;
	}
	actual = fread(buffer, 1, length, file);
	if (actual == 0 || (actual < length && ferror(file))) {
		noct_error(env, N_TR("File read error."));
		goto cleanup;
	}
	if (!noct_make_packed(env, &ret, NOCT_PACKED_UINT8, actual, actual,
			      buffer))
		goto cleanup;
	transferred = true;
	if (!noct_set_return(env, &ret))
		goto cleanup;
	ok = true;
cleanup:
	if (!transferred && buffer != NULL)
		noct_free(buffer);
	(void)noct_unpin_local(env, 3, &file_value, &length_value, &ret);
	return ok;
}

static bool
cfunc_File_write(NoctEnv *env)
{
	NoctValue file_value, data, offset_value, length_value, ret;
	FILE *file;
	size_t offset, length, packed_size;
	void *buffer;
	bool ok = false;

	if (!noct_pin_local(env, 5, &file_value, &data, &offset_value,
			    &length_value, &ret))
		return false;
	if (!noct_get_arg_check_dict(env, 0, &file_value) ||
	    !noct_get_arg_check_packed(env, 1, &data, NOCT_PACKED_UINT8) ||
	    !noct_get_arg_check_int_long(env, 2, &offset_value, &offset) ||
	    !noct_get_arg_check_int_long(env, 3, &length_value, &length) ||
	    !get_file(env, &file_value, &file) ||
	    !noct_get_packed_size(env, &data, &packed_size))
		goto cleanup;
	if (offset > packed_size || length > packed_size - offset) {
		noct_error(env, N_TR("Offset is out-of-range."));
		goto cleanup;
	}
	if (!noct_get_packed_pointer(env, &data, &buffer))
		goto cleanup;
	if (length != 0 && fwrite((char *)buffer + offset, 1, length, file) !=
			   length) {
		noct_error(env, N_TR("File write error."));
		goto cleanup;
	}
	if (!noct_set_return_make_int(env, &ret, 1))
		goto cleanup;
	ok = true;
cleanup:
	(void)noct_unpin_local(env, 5, &file_value, &data, &offset_value,
			     &length_value, &ret);
	return ok;
}

static bool
cfunc_FileUtil_checkFileExists(NoctEnv *env)
{
	NoctValue path, ret;
	const char *path_s;
	bool ok = false;

	if (!noct_pin_local(env, 2, &path, &ret))
		return false;
	if (!noct_get_arg_check_string(env, 0, &path, &path_s) ||
	    !noct_set_return_make_int(env, &ret,
				      access(path_s, F_OK) == 0 ? 1 : 0))
		goto cleanup;
	ok = true;
cleanup:
	(void)noct_unpin_local(env, 2, &path, &ret);
	return ok;
}

static bool
cfunc_FileUtil_getFileSize(NoctEnv *env)
{
	NoctValue path, ret;
	const char *path_s;
	FILE *file = NULL;
	long size;
	bool ok = false;

	if (!noct_pin_local(env, 2, &path, &ret))
		return false;
	if (!noct_get_arg_check_string(env, 0, &path, &path_s))
		goto cleanup;
	file = fopen(path_s, "rb");
	if (file == NULL) {
		ok = noct_set_return_make_int(env, &ret, 0);
		goto cleanup;
	}
	if (fseek(file, 0, SEEK_END) != 0 || (size = ftell(file)) < 0) {
		noct_error(env, N_TR("Cannot determine file size."));
		goto cleanup;
	}
	if (!noct_set_return_make_int_long(env, &ret, (size_t)size))
		goto cleanup;
	ok = true;
cleanup:
	if (file != NULL)
		(void)fclose(file);
	(void)noct_unpin_local(env, 2, &path, &ret);
	return ok;
}

static bool
cfunc_FileUtil_readText(NoctEnv *env)
{
	NoctValue path, ret;
	const char *path_s;
	FILE *file = NULL;
	char *data = NULL;
	long length;
	bool ok = false;

	if (!noct_pin_local(env, 2, &path, &ret))
		return false;
	if (!noct_get_arg_check_string(env, 0, &path, &path_s))
		goto cleanup;
	file = fopen(path_s, "rb");
	if (file == NULL) {
		noct_error(env, N_TR("Cannot open file %s."), path_s);
		goto cleanup;
	}
	if (fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) < 0 ||
	    fseek(file, 0, SEEK_SET) != 0) {
		noct_error(env, N_TR("Cannot determine file size."));
		goto cleanup;
	}
	data = noct_malloc((size_t)length + 1U);
	if (data == NULL) {
		noct_error(env, N_TR("Out of memory."));
		goto cleanup;
	}
	if (fread(data, 1, (size_t)length, file) != (size_t)length) {
		noct_error(env, N_TR("Cannot read file %s."), path_s);
		goto cleanup;
	}
	data[length] = '\0';
	if (!noct_set_return_make_string(env, &ret, data))
		goto cleanup;
	ok = true;
cleanup:
	if (file != NULL)
		(void)fclose(file);
	if (data != NULL)
		noct_free(data);
	(void)noct_unpin_local(env, 2, &path, &ret);
	return ok;
}

static bool
cfunc_FileUtil_writeText(NoctEnv *env)
{
	NoctValue path, text, ret;
	const char *path_s, *text_s;
	FILE *file = NULL;
	size_t length;
	bool ok = false;

	if (!noct_pin_local(env, 3, &path, &text, &ret))
		return false;
	if (!noct_get_arg_check_string(env, 0, &path, &path_s) ||
	    !noct_get_arg_check_string(env, 1, &text, &text_s))
		goto cleanup;
	file = fopen(path_s, "wb");
	if (file == NULL) {
		noct_error(env, N_TR("Cannot open file %s."), path_s);
		goto cleanup;
	}
	length = strlen(text_s);
	if (fwrite(text_s, 1, length, file) != length || fflush(file) != 0) {
		noct_error(env, N_TR("Cannot write file %s."), path_s);
		goto cleanup;
	}
	if (!noct_set_return_make_int(env, &ret, 1))
		goto cleanup;
	ok = true;
cleanup:
	if (file != NULL && fclose(file) != 0 && ok) {
		noct_error(env, N_TR("Cannot close file %s."), path_s);
		ok = false;
	}
	(void)noct_unpin_local(env, 3, &path, &text, &ret);
	return ok;
}

static bool
cfunc_FileUtil_readForEachLine(NoctEnv *env)
{
	char buffer[8192];
	NoctValue path, function_value, line, ret;
	NoctFunc *function;
	const char *path_s;
	FILE *file = NULL;
	bool ok = false;

	if (!noct_pin_local(env, 4, &path, &function_value, &line, &ret))
		return false;
	if (!noct_get_arg_check_string(env, 0, &path, &path_s) ||
	    !noct_get_arg_check_func(env, 1, &function_value, &function))
		goto cleanup;
	file = fopen(path_s, "rb");
	if (file == NULL) {
		noct_error(env, N_TR("Cannot open file %s."), path_s);
		goto cleanup;
	}
	while (fgets(buffer, sizeof(buffer), file) != NULL) {
		size_t length = strlen(buffer);

		if (length != 0 && buffer[length - 1] == '\n')
			buffer[length - 1] = '\0';
		if (!noct_make_string(env, &line, buffer) ||
		    !noct_call(env, function, 1, &line, &ret))
			goto cleanup;
	}
	if (ferror(file)) {
		noct_error(env, N_TR("Cannot read file %s."), path_s);
		goto cleanup;
	}
	if (!noct_set_return_make_int(env, &ret, 1))
		goto cleanup;
	ok = true;
cleanup:
	if (file != NULL)
		(void)fclose(file);
	(void)noct_unpin_local(env, 4, &path, &function_value, &line, &ret);
	return ok;
}

static bool
cfunc_FileUtil_writeForEachLine(NoctEnv *env)
{
	NoctValue path, lines, line, ret;
	const char *path_s, *text;
	FILE *file = NULL;
	size_t count, index;
	bool ok = false;

	if (!noct_pin_local(env, 4, &path, &lines, &line, &ret))
		return false;
	if (!noct_get_arg_check_string(env, 0, &path, &path_s) ||
	    !noct_get_arg_check_array(env, 1, &lines) ||
	    !noct_get_array_size(env, &lines, &count))
		goto cleanup;
	file = fopen(path_s, "wb");
	if (file == NULL) {
		noct_error(env, N_TR("Cannot open file %s."), path_s);
		goto cleanup;
	}
	for (index = 0; index < count; index++) {
		if (!noct_get_array_elem(env, &lines, index, &line) ||
		    !noct_get_string(env, &line, &text) ||
		    fprintf(file, "%s\n", text) < 0)
			goto cleanup;
	}
	if (fflush(file) != 0) {
		noct_error(env, N_TR("Cannot write file %s."), path_s);
		goto cleanup;
	}
	if (!noct_set_return_make_int(env, &ret, 1))
		goto cleanup;
	ok = true;
cleanup:
	if (file != NULL && fclose(file) != 0 && ok) {
		noct_error(env, N_TR("Cannot close file %s."), path_s);
		ok = false;
	}
	(void)noct_unpin_local(env, 4, &path, &lines, &line, &ret);
	return ok;
}
