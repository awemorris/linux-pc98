/*
 * PC98BE adapters for the selected musl string scanner.
 * Copyright (C) 2026 Awe Morris
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "boot98-musl-floatscan.h"

#include <stdint.h>

double fmod(double value, double divisor);
double scalbn(double value, int exponent);

int
__uflow(FILE *stream)
{
	(void)stream;
	return EOF;
}

long double
fabsl(long double value)
{
	union {
		long double value;
		uint64_t bits;
	} shape = { value };

	shape.bits &= 0x7fffffffffffffffULL;
	return shape.value;
}

long double
copysignl(long double magnitude, long double sign)
{
	union {
		long double value;
		uint64_t bits;
	} left = { magnitude }, right = { sign };

	left.bits &= 0x7fffffffffffffffULL;
	left.bits |= right.bits & 0x8000000000000000ULL;
	return left.value;
}

long double
scalbnl(long double value, int exponent)
{
	return (long double)scalbn((double)value, exponent);
}

long double
fmodl(long double value, long double divisor)
{
	return (long double)fmod((double)value, (double)divisor);
}
