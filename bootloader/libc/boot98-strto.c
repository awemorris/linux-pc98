/*
 * PC-98 Bootstrap Environment freestanding C library
 * Copyright (C) 2026 Awe Morris
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>

static const char *
skip_space(const char *string)
{
	while (isspace((unsigned char)*string))
		string++;
	return string;
}

static int
digit_value(int character)
{
	if (character >= '0' && character <= '9')
		return character - '0';
	if (character >= 'a' && character <= 'z')
		return character - 'a' + 10;
	if (character >= 'A' && character <= 'Z')
		return character - 'A' + 10;
	return -1;
}

static unsigned long long
parse_magnitude(const char *string, char **end, int base, int *negative,
	int *overflow)
{
	const char *start = string;
	const char *cursor = skip_space(string);
	unsigned long long value = 0;
	unsigned long long cutoff;
	unsigned int cutlim;
	int any = 0;

	*negative = 0;
	*overflow = 0;
	if (*cursor == '+' || *cursor == '-') {
		*negative = *cursor == '-';
		cursor++;
	}
	if ((base == 0 || base == 16) && cursor[0] == '0' &&
	    (cursor[1] == 'x' || cursor[1] == 'X')) {
		base = 16;
		cursor += 2;
	} else if (base == 0) {
		base = cursor[0] == '0' ? 8 : 10;
	}
	if (base < 2 || base > 36) {
		errno = EINVAL;
		if (end != NULL)
			*end = (char *)start;
		return 0;
	}
	cutoff = ULLONG_MAX / (unsigned int)base;
	cutlim = (unsigned int)(ULLONG_MAX % (unsigned int)base);
	for (;;) {
		int digit = digit_value((unsigned char)*cursor);
		if (digit < 0 || digit >= base)
			break;
		if (value > cutoff ||
		    (value == cutoff && (unsigned int)digit > cutlim)) {
			any = -1;
			value = ULLONG_MAX;
		} else if (any >= 0) {
			any = 1;
			value = value * (unsigned int)base + (unsigned int)digit;
		}
		cursor++;
	}
	if (any == 0)
		cursor = start;
	if (any < 0)
		*overflow = 1;
	if (any < 0)
		errno = ERANGE;
	if (end != NULL)
		*end = (char *)cursor;
	return value;
}

unsigned long long
strtoull(const char *string, char **end, int base)
{
	unsigned long long value;
	int negative;
	int overflow;

	value = parse_magnitude(string, end, base, &negative, &overflow);
	if (overflow)
		return ULLONG_MAX;
	return negative ? 0U - value : value;
}

long long
strtoll(const char *string, char **end, int base)
{
	unsigned long long value;
	unsigned long long limit;
	int negative;
	int overflow;

	value = parse_magnitude(string, end, base, &negative, &overflow);
	limit = negative ? (unsigned long long)LLONG_MAX + 1U :
		(unsigned long long)LLONG_MAX;
	if (overflow || value > limit) {
		errno = ERANGE;
		return negative ? LLONG_MIN : LLONG_MAX;
	}
	if (negative && value == (unsigned long long)LLONG_MAX + 1U)
		return LLONG_MIN;
	return negative ? -(long long)value : (long long)value;
}

unsigned long
strtoul(const char *string, char **end, int base)
{
	unsigned long long value;
	int negative;
	int overflow;

	value = parse_magnitude(string, end, base, &negative, &overflow);
	if (overflow || value > ULONG_MAX) {
		errno = ERANGE;
		return ULONG_MAX;
	}
	return negative ? 0UL - (unsigned long)value : (unsigned long)value;
}

long
strtol(const char *string, char **end, int base)
{
	long long value = strtoll(string, end, base);
	if (value > LONG_MAX) {
		errno = ERANGE;
		return LONG_MAX;
	}
	if (value < LONG_MIN) {
		errno = ERANGE;
		return LONG_MIN;
	}
	return (long)value;
}

int atoi(const char *string) { return (int)strtol(string, NULL, 10); }
long atol(const char *string) { return strtol(string, NULL, 10); }
long long atoll(const char *string) { return strtoll(string, NULL, 10); }
int abs(int value) { return value < 0 ? -value : value; }
long labs(long value) { return value < 0 ? -value : value; }

double
strtod(const char *string, char **end)
{
	const char *cursor = skip_space(string);
	double value = 0.0;
	double scale = 1.0;
	int negative = 0;
	int any = 0;

	if (*cursor == '+' || *cursor == '-') {
		negative = *cursor == '-';
		cursor++;
	}
	while (isdigit((unsigned char)*cursor)) {
		value = value * 10.0 + (double)(*cursor++ - '0');
		any = 1;
	}
	if (*cursor == '.') {
		cursor++;
		while (isdigit((unsigned char)*cursor)) {
			scale *= 0.1;
			value += (double)(*cursor++ - '0') * scale;
			any = 1;
		}
	}
	if (end != NULL)
		*end = (char *)(any ? cursor : string);
	return negative ? -value : value;
}

double atof(const char *string) { return strtod(string, NULL); }

static unsigned int random_state = 1;
void srand(unsigned int seed) { random_state = seed; }
int
rand(void)
{
	random_state = random_state * 1103515245U + 12345U;
	return (int)((random_state >> 1) & RAND_MAX);
}
