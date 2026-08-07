/*
 * PC-98 Bootstrap Environment freestanding C library
 * Copyright (C) 2026 Awe Morris
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <errno.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

int boot98_errno;

static FILE standard_input;
static FILE standard_output;
static FILE standard_error;
FILE *stdin = &standard_input;
FILE *stdout = &standard_output;
FILE *stderr = &standard_error;

__attribute__((weak)) size_t
boot98_console_write_bytes(const char *bytes, size_t length)
{
	(void)bytes;
	return length;
}

int
printf(const char *format, ...)
{
	char buffer[256];
	va_list arguments;
	int length;
	size_t write_length;

	va_start(arguments, format);
	length = vsnprintf(buffer, sizeof(buffer), format, arguments);
	va_end(arguments);
	write_length = length < 0 ? 0U : (size_t)length;
	if (write_length >= sizeof(buffer))
		write_length = sizeof(buffer) - 1U;
	boot98_console_write_bytes(buffer, write_length);
	return length;
}

int
putchar(int character)
{
	char byte = (char)character;
	return boot98_console_write_bytes(&byte, 1) == 1 ?
		(unsigned char)byte : EOF;
}

int
puts(const char *string)
{
	size_t length = strlen(string);
	if (boot98_console_write_bytes(string, length) != length ||
	    boot98_console_write_bytes("\n", 1) != 1)
		return EOF;
	return 0;
}

__attribute__((weak)) int
getc(FILE *stream)
{
	if (stream != NULL) {
		stream->eof = 1;
		stream->error = 1;
	}
	errno = EIO;
	return EOF;
}

__attribute__((weak)) size_t
fread(void *buffer, size_t size, size_t count, FILE *stream)
{
	(void)buffer;
	(void)size;
	(void)count;
	if (stream != NULL) {
		stream->eof = 1;
		stream->error = 1;
	}
	errno = EIO;
	return 0;
}

__attribute__((weak)) size_t
fwrite(const void *buffer, size_t size, size_t count, FILE *stream)
{
	size_t total;
	if (size != 0 && count > (size_t)-1 / size) {
		errno = EINVAL;
		return 0;
	}
	total = size * count;
	if (stream == stdout || stream == stderr)
		return boot98_console_write_bytes(buffer, total) == total ? count : 0;
	if (stream != NULL)
		stream->error = 1;
	errno = EIO;
	return 0;
}

int ferror(FILE *stream) { return stream == NULL ? 1 : stream->error; }
void
clearerr(FILE *stream)
{
	if (stream != NULL) {
		stream->error = 0;
		stream->eof = 0;
	}
}

int
fprintf(FILE *stream, const char *format, ...)
{
	char stack_buffer[256];
	char *buffer = stack_buffer;
	va_list arguments;
	int length;
	size_t written;

	va_start(arguments, format);
	length = vsnprintf(stack_buffer, sizeof(stack_buffer), format, arguments);
	va_end(arguments);
	if (length < 0)
		return EOF;
	if ((size_t)length >= sizeof(stack_buffer)) {
		buffer = malloc((size_t)length + 1U);
		if (buffer == NULL) {
			errno = ENOMEM;
			return EOF;
		}
		va_start(arguments, format);
		(void)vsnprintf(buffer, (size_t)length + 1U, format, arguments);
		va_end(arguments);
	}
	written = fwrite(buffer, 1, (size_t)length, stream);
	if (buffer != stack_buffer)
		free(buffer);
	return written == (size_t)length ? length : EOF;
}

int
sscanf(const char *string, const char *format, ...)
{
	va_list arguments;
	char *end;
	unsigned long value;
	int result = 0;

	while (*format == ' ' || *format == '\t')
		format++;
	if (*format++ != '%')
		return 0;
	va_start(arguments, format);
	if (*format == 'x' || *format == 'X') {
		unsigned int *destination = va_arg(arguments, unsigned int *);
		value = strtoul(string, &end, 16);
		if (end != string) {
			*destination = (unsigned int)value;
			result = 1;
		}
	} else if (*format == 'u') {
		unsigned int *destination = va_arg(arguments, unsigned int *);
		value = strtoul(string, &end, 10);
		if (end != string) {
			*destination = (unsigned int)value;
			result = 1;
		}
	} else if (*format == 'd' || *format == 'i') {
		int *destination = va_arg(arguments, int *);
		long signed_value = strtol(string, &end, *format == 'i' ? 0 : 10);
		if (end != string) {
			*destination = (int)signed_value;
			result = 1;
		}
	}
	va_end(arguments);
	return result;
}

__attribute__((weak)) int
access(const char *path, int mode)
{
	(void)path;
	(void)mode;
	errno = ENOENT;
	return -1;
}

int isatty(int descriptor) { (void)descriptor; return 0; }
int fileno(void *stream) { (void)stream; return -1; }
time_t time(time_t *result) { if (result != NULL) *result = 0; return 0; }

__attribute__((weak, noreturn)) void
boot98_libc_panic(const char *message)
{
	(void)message;
	for (;;)
		__asm__ volatile ("hlt");
}

void
abort(void)
{
	boot98_libc_panic("abort");
}

void
exit(int status)
{
	(void)status;
	boot98_libc_panic("exit");
}

void
boot98_assert_fail(const char *expression, const char *file, int line)
{
	char message[160];
	snprintf(message, sizeof(message), "assertion failed: %s (%s:%d)",
		expression, file, line);
	boot98_libc_panic(message);
}
