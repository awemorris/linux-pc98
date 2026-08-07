/*
 * PC-98 Bootstrap Environment freestanding C library
 * Copyright (C) 2026 Awe Morris
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <limits.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

struct format_output {
	char *buffer;
	size_t size;
	size_t length;
};

static void
emit_character(struct format_output *output, char character)
{
	if (output->size != 0 && output->length + 1U < output->size)
		output->buffer[output->length] = character;
	output->length++;
}

static void
emit_repeat(struct format_output *output, char character, size_t count)
{
	while (count-- != 0)
		emit_character(output, character);
}

static void
emit_bytes(struct format_output *output, const char *text, size_t length)
{
	while (length-- != 0)
		emit_character(output, *text++);
}

static size_t
unsigned_digits(char *reverse, uint64_t value, unsigned int base, int upper)
{
	const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
	size_t length = 0;

	do {
		reverse[length++] = digits[(unsigned int)(value % base)];
		value /= base;
	} while (value != 0);
	return length;
}

static void
emit_integer(struct format_output *output, uint64_t value, int negative,
	unsigned int base, int upper, int alternate, int left, int plus,
	int space, int zero, int width, int precision)
{
	char reverse[32];
	char prefix[3];
	size_t digits = unsigned_digits(reverse, value, base, upper);
	size_t zeroes = 0;
	size_t prefix_length = 0;
	size_t total;
	size_t index;

	if (precision == 0 && value == 0)
		digits = 0;
	if (negative)
		prefix[prefix_length++] = '-';
	else if (plus)
		prefix[prefix_length++] = '+';
	else if (space)
		prefix[prefix_length++] = ' ';
	if (alternate && value != 0 && base == 16) {
		prefix[prefix_length++] = '0';
		prefix[prefix_length++] = upper ? 'X' : 'x';
	} else if (alternate && base == 8 &&
		   (digits == 0 || reverse[digits - 1U] != '0')) {
		prefix[prefix_length++] = '0';
	}
	if (precision > 0 && (size_t)precision > digits)
		zeroes = (size_t)precision - digits;
	if (zero && !left && precision < 0 && width > 0 &&
	    (size_t)width > prefix_length + digits)
		zeroes = (size_t)width - prefix_length - digits;
	total = prefix_length + zeroes + digits;
	if (!left && width > 0 && (size_t)width > total)
		emit_repeat(output, ' ', (size_t)width - total);
	emit_bytes(output, prefix, prefix_length);
	emit_repeat(output, '0', zeroes);
	for (index = digits; index != 0; index--)
		emit_character(output, reverse[index - 1U]);
	if (left && width > 0 && (size_t)width > total)
		emit_repeat(output, ' ', (size_t)width - total);
}

enum length_modifier {
	LENGTH_DEFAULT,
	LENGTH_CHAR,
	LENGTH_SHORT,
	LENGTH_LONG,
	LENGTH_LONG_LONG,
	LENGTH_SIZE
};

int
vsnprintf(char *buffer, size_t size, const char *format, va_list arguments)
{
	struct format_output output = { buffer, size, 0 };

	while (*format != '\0') {
		int alternate, left, plus, space, zero, width, precision;
		enum length_modifier length;
		char conversion;

		if (*format != '%') {
			emit_character(&output, *format++);
			continue;
		}
		format++;
		if (*format == '%') {
			emit_character(&output, *format++);
			continue;
		}
		alternate = left = plus = space = zero = 0;
		for (;;) {
			if (*format == '#') alternate = 1;
			else if (*format == '-') left = 1;
			else if (*format == '+') plus = 1;
			else if (*format == ' ') space = 1;
			else if (*format == '0') zero = 1;
			else break;
			format++;
		}
		width = 0;
		if (*format == '*') {
			width = va_arg(arguments, int);
			format++;
			if (width < 0) {
				left = 1;
				width = -width;
			}
		} else {
			while (*format >= '0' && *format <= '9')
				width = width * 10 + (*format++ - '0');
		}
		precision = -1;
		if (*format == '.') {
			precision = 0;
			format++;
			if (*format == '*') {
				precision = va_arg(arguments, int);
				format++;
				if (precision < 0)
					precision = -1;
			} else {
				while (*format >= '0' && *format <= '9')
					precision = precision * 10 + (*format++ - '0');
			}
		}
		length = LENGTH_DEFAULT;
		if (*format == 'h') {
			format++;
			length = *format == 'h' ? (format++, LENGTH_CHAR) : LENGTH_SHORT;
		} else if (*format == 'l') {
			format++;
			length = *format == 'l' ?
				(format++, LENGTH_LONG_LONG) : LENGTH_LONG;
		} else if (*format == 'z' || *format == 't' || *format == 'j') {
			length = *format == 'j' ? LENGTH_LONG_LONG : LENGTH_SIZE;
			format++;
		}
		conversion = *format == '\0' ? '\0' : *format++;
		if (conversion == 's') {
			const char *text = va_arg(arguments, const char *);
			size_t text_length;
			if (text == NULL)
				text = "(null)";
			text_length = precision >= 0 ?
				strnlen(text, (size_t)precision) : strlen(text);
			if (!left && width > 0 && (size_t)width > text_length)
				emit_repeat(&output, ' ', (size_t)width - text_length);
			emit_bytes(&output, text, text_length);
			if (left && width > 0 && (size_t)width > text_length)
				emit_repeat(&output, ' ', (size_t)width - text_length);
		} else if (conversion == 'c') {
			if (!left && width > 1)
				emit_repeat(&output, ' ', (size_t)width - 1U);
			emit_character(&output, (char)va_arg(arguments, int));
			if (left && width > 1)
				emit_repeat(&output, ' ', (size_t)width - 1U);
		} else if (conversion == 'd' || conversion == 'i') {
			int64_t signed_value;
			if (length == LENGTH_LONG_LONG)
				signed_value = va_arg(arguments, long long);
			else if (length == LENGTH_LONG)
				signed_value = va_arg(arguments, long);
			else if (length == LENGTH_SIZE)
				signed_value = va_arg(arguments, ptrdiff_t);
			else
				signed_value = va_arg(arguments, int);
			emit_integer(&output,
				signed_value < 0 ? 0U - (uint64_t)signed_value :
				(uint64_t)signed_value,
				signed_value < 0, 10, 0, alternate, left, plus,
				space, zero, width, precision);
		} else if (conversion == 'u' || conversion == 'x' ||
			   conversion == 'X' || conversion == 'o') {
			uint64_t value;
			unsigned int base = conversion == 'o' ? 8U :
				(conversion == 'u' ? 10U : 16U);
			if (length == LENGTH_LONG_LONG)
				value = va_arg(arguments, unsigned long long);
			else if (length == LENGTH_LONG)
				value = va_arg(arguments, unsigned long);
			else if (length == LENGTH_SIZE)
				value = va_arg(arguments, size_t);
			else
				value = va_arg(arguments, unsigned int);
			emit_integer(&output, value, 0, base, conversion == 'X',
				alternate, left, 0, 0, zero, width, precision);
		} else if (conversion == 'p') {
			uintptr_t value = (uintptr_t)va_arg(arguments, void *);
			emit_integer(&output, value, 0, 16, 0, 1, left, 0, 0,
				zero, width, precision);
		} else if (conversion == 'f' || conversion == 'F' ||
			   conversion == 'e' || conversion == 'E' ||
			   conversion == 'g' || conversion == 'G') {
			(void)va_arg(arguments, double);
			emit_bytes(&output, "<soft-float-pending>", 20);
		} else if (conversion == '\0') {
			break;
		} else {
			emit_character(&output, '%');
			emit_character(&output, conversion);
		}
	}
	if (size != 0) {
		size_t terminator = output.length < size ? output.length : size - 1U;
		buffer[terminator] = '\0';
	}
	return output.length > (size_t)INT_MAX ? INT_MAX : (int)output.length;
}

int
snprintf(char *buffer, size_t size, const char *format, ...)
{
	va_list arguments;
	int result;
	va_start(arguments, format);
	result = vsnprintf(buffer, size, format, arguments);
	va_end(arguments);
	return result;
}
