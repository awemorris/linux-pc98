/*
 * PC-98 Bootstrap Environment soft-float known-vector tests
 * Copyright (C) 2026 Awe Morris
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expression) do { if (!(expression)) return __LINE__; } while (0)

static uint64_t
double_bits(double value)
{
	union {
		double value;
		uint64_t bits;
	} shape = { value };

	return shape.bits;
}

static uint32_t
float_bits(float value)
{
	union {
		float value;
		uint32_t bits;
	} shape = { value };

	return shape.bits;
}

static int
test_arithmetic(void)
{
	volatile double a = 1.25;
	volatile double b = 2.5;
	volatile float af = 1.25f;
	volatile float bf = 2.5f;

	CHECK(double_bits(a + b) == 0x400e000000000000ULL);
	CHECK(double_bits(b - a) == 0x3ff4000000000000ULL);
	CHECK(double_bits(a * b) == 0x4009000000000000ULL);
	CHECK(double_bits(b / a) == 0x4000000000000000ULL);
	CHECK(float_bits(af + bf) == 0x40700000U);
	CHECK(float_bits(bf - af) == 0x3fa00000U);
	CHECK(float_bits(af * bf) == 0x40480000U);
	CHECK(float_bits(bf / af) == 0x40000000U);
	CHECK((int)(double)-17 == -17);
	CHECK((unsigned int)(double)123 == 123U);
	CHECK((double)(int64_t)-123456789 == -123456789.0);
	CHECK((double)(uint64_t)4000000000U == 4000000000.0);
	return 0;
}

static int
test_conversion(void)
{
	char *end;
	double value;

	errno = 0;
	value = strtod("  -2.25tail", &end);
	CHECK(double_bits(value) == 0xc002000000000000ULL &&
		strcmp(end, "tail") == 0 && errno == 0);
	CHECK(double_bits(strtod("0.383177570093458", &end)) ==
		0x3fd885fb37072d76ULL && *end == '\0');
	CHECK(double_bits(strtod("1e20", &end)) == 0x4415af1d78b58c40ULL &&
		*end == '\0');
	CHECK(double_bits(strtod("1e-20", &end)) == 0x3bc79ca10c924223ULL &&
		*end == '\0');
	CHECK(double_bits(strtod("0x1.8p+1", &end)) ==
		0x4008000000000000ULL && *end == '\0');
	value = strtod("inf", &end);
	CHECK((double_bits(value) & 0x7fffffffffffffffULL) ==
		0x7ff0000000000000ULL && *end == '\0');
	value = strtod("nan", &end);
	CHECK((double_bits(value) & 0x7ff0000000000000ULL) ==
		0x7ff0000000000000ULL &&
		(double_bits(value) & 0x000fffffffffffffULL) != 0 &&
		*end == '\0');
	errno = 0;
	value = strtod("1e9999", &end);
	CHECK((double_bits(value) & 0x7fffffffffffffffULL) ==
		0x7ff0000000000000ULL && errno == ERANGE);
	errno = 0;
	value = strtod("1e-9999", &end);
	CHECK((double_bits(value) & 0x7fffffffffffffffULL) == 0 &&
		errno == ERANGE);
	return 0;
}

static int
test_math(void)
{
	volatile float half = 0.5f;
	float invalid;

	CHECK(float_bits(sqrtf(9.0f)) == 0x40400000U);
	CHECK(float_bits(sinf(half)) == 0x3ef57744U);
	CHECK(float_bits(cosf(half)) == 0x3f60a940U);
	CHECK(float_bits(tanf(half)) == 0x3f0bda7bU);
	invalid = sqrtf(-1.0f);
	CHECK(isnan(invalid));
	CHECK((float_bits(invalid) & 0x7f800000U) == 0x7f800000U &&
		(float_bits(invalid) & 0x007fffffU) != 0);
	return 0;
}

static int
test_format(void)
{
	char buffer[96];
	volatile float numerator = 123.0f;
	volatile float denominator = 321.0f;
	volatile double long_result = 123.0 / 321.0;

	CHECK(snprintf(buffer, sizeof(buffer), "%.7g",
		(double)(numerator / denominator)) == 9 &&
		strcmp(buffer, "0.3831776") == 0);
	CHECK(snprintf(buffer, sizeof(buffer), "%.15g", long_result) == 17 &&
		strcmp(buffer, "0.383177570093458") == 0);
	CHECK(snprintf(buffer, sizeof(buffer), "%.7g", 1.0e20) == 5 &&
		strcmp(buffer, "1e+20") == 0);
	CHECK(snprintf(buffer, sizeof(buffer), "%f", 1.5) == 8 &&
		strcmp(buffer, "1.500000") == 0);
	CHECK(snprintf(buffer, sizeof(buffer), "%.2e", 123.0) == 8 &&
		strcmp(buffer, "1.23e+02") == 0);
	CHECK(snprintf(buffer, sizeof(buffer), "%g", -0.0) == 2 &&
		strcmp(buffer, "-0") == 0);
	return 0;
}

int
main(void)
{
	int result;

	result = test_arithmetic();
	if (result != 0)
		return result;
	result = test_conversion();
	if (result != 0)
		return result;
	result = test_math();
	if (result != 0)
		return result;
	result = test_format();
	if (result != 0)
		return result;
	return 0;
}
