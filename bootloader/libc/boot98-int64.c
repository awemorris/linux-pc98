/*
 * PC-98 Bootstrap Environment freestanding C library
 * Copyright (C) 2026 Awe Morris
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdint.h>

static uint64_t
unsigned_divide(uint64_t numerator, uint64_t denominator, uint64_t *remainder)
{
	uint64_t quotient = 0;
	uint64_t rest = 0;
	int bit;

	if (denominator == 0) {
		if (remainder != 0)
			*remainder = numerator;
		return UINT64_MAX;
	}
	for (bit = 0; bit < 64; bit++) {
		rest = (rest << 1) | (numerator >> 63);
		numerator <<= 1;
		quotient <<= 1;
		if (rest >= denominator) {
			rest -= denominator;
			quotient |= 1;
		}
	}
	if (remainder != 0)
		*remainder = rest;
	return quotient;
}

uint64_t
__udivdi3(uint64_t numerator, uint64_t denominator)
{
	return unsigned_divide(numerator, denominator, 0);
}

uint64_t
__umoddi3(uint64_t numerator, uint64_t denominator)
{
	uint64_t remainder;
	(void)unsigned_divide(numerator, denominator, &remainder);
	return remainder;
}

uint64_t
__udivmoddi4(uint64_t numerator, uint64_t denominator, uint64_t *remainder)
{
	return unsigned_divide(numerator, denominator, remainder);
}

int64_t
__divdi3(int64_t numerator, int64_t denominator)
{
	int negative = (numerator < 0) != (denominator < 0);
	uint64_t left = numerator < 0 ? 0U - (uint64_t)numerator :
		(uint64_t)numerator;
	uint64_t right = denominator < 0 ? 0U - (uint64_t)denominator :
		(uint64_t)denominator;
	uint64_t result = unsigned_divide(left, right, 0);
	return negative ? (int64_t)(0U - result) : (int64_t)result;
}

int64_t
__moddi3(int64_t numerator, int64_t denominator)
{
	uint64_t remainder;
	uint64_t left = numerator < 0 ? 0U - (uint64_t)numerator :
		(uint64_t)numerator;
	uint64_t right = denominator < 0 ? 0U - (uint64_t)denominator :
		(uint64_t)denominator;
	(void)unsigned_divide(left, right, &remainder);
	return numerator < 0 ? (int64_t)(0U - remainder) : (int64_t)remainder;
}

uint64_t
__muldi3(uint64_t left, uint64_t right)
{
	uint64_t result = 0;
	while (right != 0) {
		if ((right & 1U) != 0)
			result += left;
		left <<= 1;
		right >>= 1;
	}
	return result;
}

union split64 {
	uint64_t value;
	struct {
		uint32_t low;
		uint32_t high;
	} word;
};

uint64_t
__ashldi3(uint64_t value, int count)
{
	union split64 input;
	union split64 output;
	unsigned int shift = (unsigned int)count & 63U;

	input.value = value;
	if (shift == 0)
		return value;
	if (shift < 32U) {
		output.word.low = input.word.low << shift;
		output.word.high = (input.word.high << shift) |
			(input.word.low >> (32U - shift));
	} else {
		output.word.low = 0;
		output.word.high = input.word.low << (shift - 32U);
	}
	return output.value;
}

uint64_t
__lshrdi3(uint64_t value, int count)
{
	union split64 input;
	union split64 output;
	unsigned int shift = (unsigned int)count & 63U;

	input.value = value;
	if (shift == 0)
		return value;
	if (shift < 32U) {
		output.word.high = input.word.high >> shift;
		output.word.low = (input.word.low >> shift) |
			(input.word.high << (32U - shift));
	} else {
		output.word.high = 0;
		output.word.low = input.word.high >> (shift - 32U);
	}
	return output.value;
}

int64_t
__ashrdi3(int64_t value, int count)
{
	union split64 input;
	union split64 output;
	unsigned int shift = (unsigned int)count & 63U;
	int32_t signed_high;

	input.value = (uint64_t)value;
	signed_high = (int32_t)input.word.high;
	if (shift == 0)
		return value;
	if (shift < 32U) {
		output.word.high = (uint32_t)(signed_high >> shift);
		output.word.low = (input.word.low >> shift) |
			(input.word.high << (32U - shift));
	} else {
		output.word.high = signed_high < 0 ? UINT32_MAX : 0;
		output.word.low = (uint32_t)(signed_high >> (shift - 32U));
	}
	return (int64_t)output.value;
}
