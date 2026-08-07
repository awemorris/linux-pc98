/* Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef BOOT98_STDIO_H
#define BOOT98_STDIO_H

#include <stdarg.h>
#include <stddef.h>
#include <sys/types.h>

#define EOF (-1)
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

typedef struct boot98_file {
	void *context;
	size_t position;
	int error;
	int eof;
} FILE;

extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

int printf(const char *format, ...);
int snprintf(char *buffer, size_t size, const char *format, ...);
int vsnprintf(char *buffer, size_t size, const char *format, va_list arguments);
int sscanf(const char *string, const char *format, ...);
int putchar(int character);
int puts(const char *string);
int getc(FILE *stream);
size_t fread(void *buffer, size_t size, size_t count, FILE *stream);
size_t fwrite(const void *buffer, size_t size, size_t count, FILE *stream);
int ferror(FILE *stream);
void clearerr(FILE *stream);

#endif
