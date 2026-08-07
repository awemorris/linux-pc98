/*
 * PC-98 Bootstrap Environment freestanding C library tests
 * Copyright (C) 2026 Awe Morris
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "../libc/boot98-heap.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char console_output[512];
static size_t console_length;

size_t
boot98_console_write_bytes(const char *bytes, size_t length)
{
	size_t index;
	for (index = 0; index < length && console_length < sizeof(console_output);
	     index++)
		console_output[console_length++] = bytes[index];
	return length;
}

#define CHECK(expression) do { if (!(expression)) return __LINE__; } while (0)

extern uint64_t __udivdi3(uint64_t numerator, uint64_t denominator);
extern uint64_t __umoddi3(uint64_t numerator, uint64_t denominator);
extern uint64_t __udivmoddi4(uint64_t numerator, uint64_t denominator,
	uint64_t *remainder);
extern int64_t __divdi3(int64_t numerator, int64_t denominator);
extern int64_t __moddi3(int64_t numerator, int64_t denominator);
extern uint64_t __muldi3(uint64_t left, uint64_t right);
extern uint64_t __ashldi3(uint64_t value, int count);
extern uint64_t __lshrdi3(uint64_t value, int count);
extern int64_t __ashrdi3(int64_t value, int count);

static int
test_memory_and_strings(void)
{
	char buffer[32];
	char overlap[16] = "0123456789";
	char padded[8];
	const char search_text[] = "abcba";
	const char substring_text[] = "bootstrap";

	memset(buffer, 'x', sizeof(buffer));
	CHECK(buffer[0] == 'x' && buffer[31] == 'x');
	memcpy(buffer, "abc", 4);
	CHECK(strcmp(buffer, "abc") == 0);
	memmove(overlap + 2, overlap, 8);
	CHECK(memcmp(overlap, "0101234567", 10) == 0);
	memmove(overlap, overlap + 2, 8);
	CHECK(memcmp(overlap, "01234567", 8) == 0);
	CHECK(strlen("hello") == 5 && strnlen("hello", 3) == 3);
	CHECK(strncmp("abcd", "abce", 3) == 0);
	CHECK(strcmp("abcd", "abce") < 0);
	strcpy(buffer, "ab");
	strcat(buffer, "cd");
	strncat(buffer, "efgh", 2);
	CHECK(strcmp(buffer, "abcdef") == 0);
	memset(padded, 'x', sizeof(padded));
	strncpy(padded, "a", sizeof(padded));
	CHECK(padded[0] == 'a' && padded[1] == '\0' && padded[7] == '\0');
	CHECK(strchr(search_text, 'b') == &search_text[1]);
	CHECK(strrchr(search_text, 'b') == &search_text[3]);
	CHECK(strstr(substring_text, "strap") == &substring_text[4]);
	return 0;
}

static int
test_ctype_and_numbers(void)
{
	char *end;
	unsigned int hexadecimal = 0;

	CHECK(isalpha('A') && isalpha('z') && !isalpha('1'));
	CHECK(isdigit('9') && isxdigit('f') && isspace('\n'));
	CHECK(ispunct('!') && tolower('Q') == 'q' && toupper('m') == 'M');
	errno = 0;
	CHECK(strtol(" -123x", &end, 10) == -123 && *end == 'x');
	CHECK(strtoul("0xff!", &end, 0) == 255 && *end == '!');
	errno = 0;
	CHECK(strtoul("-1", &end, 10) == ULONG_MAX && errno == 0 &&
		*end == '\0');
	CHECK(strtoll("-9223372036854775808", &end, 10) == LLONG_MIN &&
		*end == '\0');
	errno = 0;
	CHECK(strtoll("-18446744073709551616", &end, 10) == LLONG_MIN &&
		errno == ERANGE && *end == '\0');
	errno = 0;
	CHECK(strtoul("999999999999", &end, 10) == ULONG_MAX && errno == ERANGE);
	CHECK(atoll("4294967296") == 4294967296LL);
	CHECK(sscanf("7f", "%x", &hexadecimal) == 1 && hexadecimal == 0x7f);
	return 0;
}

static int
test_integer_helpers(void)
{
	uint64_t remainder;

	CHECK(__udivdi3(UINT64_MAX, 10) == 1844674407370955161ULL);
	CHECK(__umoddi3(UINT64_MAX, 10) == 5);
	CHECK(__udivdi3(UINT64_MAX, 0x8000000000000000ULL) == 1);
	CHECK(__umoddi3(UINT64_MAX, 0x8000000000000000ULL) ==
		0x7fffffffffffffffULL);
	CHECK(__udivmoddi4(0x8000000000000000ULL, 3, &remainder) ==
		3074457345618258602ULL && remainder == 2);
	CHECK(__divdi3(-7, 3) == -2 && __moddi3(-7, 3) == -1);
	CHECK(__divdi3(7, -3) == -2 && __moddi3(7, -3) == 1);
	CHECK(__muldi3(0x100000001ULL, 0x100000001ULL) ==
		0x200000001ULL);
	CHECK(__ashldi3(1, 40) == 0x10000000000ULL);
	CHECK(__lshrdi3(0x8000000000000000ULL, 63) == 1);
	CHECK(__ashrdi3((int64_t)0x8000000000000000ULL, 63) == -1);
	return 0;
}

static int
test_formatting(void)
{
	char buffer[64];
	int length;

	length = snprintf(buffer, sizeof(buffer), "%s %c %+d %08x", "ok", '!',
		-12, 0x3aU);
	CHECK(length == 17 && strcmp(buffer, "ok ! -12 0000003a") == 0);
	length = snprintf(buffer, sizeof(buffer), "%-5.3s:%#x:%lld", "abcdef",
		0x2aU, -9223372036854775807LL);
	CHECK(strcmp(buffer, "abc  :0x2a:-9223372036854775807") == 0);
	CHECK(length == (int)strlen(buffer));
	length = snprintf(buffer, 5, "abcdef");
	CHECK(length == 6 && strcmp(buffer, "abcd") == 0);
	CHECK(snprintf(buffer, sizeof(buffer), "%-3c:%3c", 'a', 'b') == 7 &&
		strcmp(buffer, "a  :  b") == 0);
	length = snprintf(NULL, 0, "%u", 12345U);
	CHECK(length == 5);
	console_length = 0;
	CHECK(printf("value=%d", 42) == 8);
	CHECK(console_length == 8 && memcmp(console_output, "value=42", 8) == 0);
	return 0;
}

static int
test_heap(void)
{
	static unsigned char arena[8192 + 7];
	void *a;
	void *b;
	void *c;
	char *copy;
	size_t initial_largest;
	size_t index;

	boot98_heap_init(arena + 1, sizeof(arena) - 1);
	CHECK(boot98_heap_validate());
	initial_largest = boot98_heap_largest_free();
	a = boot98_malloc(13);
	b = boot98_malloc(257);
	c = boot98_calloc(17, 3);
	CHECK(a != NULL && b != NULL && c != NULL);
	CHECK(((uintptr_t)a & 7U) == 0 && ((uintptr_t)b & 7U) == 0);
	for (index = 0; index < 51; index++)
		CHECK(((unsigned char *)c)[index] == 0);
	memset(b, 0x5a, 257);
	b = boot98_realloc(b, 600);
	CHECK(b != NULL);
	for (index = 0; index < 257; index++)
		CHECK(((unsigned char *)b)[index] == 0x5a);
	b = boot98_realloc(b, 64);
	CHECK(b != NULL && boot98_heap_validate());
	copy = boot98_strdup("Noct PC98BE");
	CHECK(copy != NULL && strcmp(copy, "Noct PC98BE") == 0);
	CHECK(boot98_heap_current() == 13 + 64 + 51 + 12);
	CHECK(boot98_heap_peak() >= boot98_heap_current());
	CHECK(boot98_calloc((size_t)-1, 2) == NULL);
	boot98_free(a);
	boot98_free(b);
	boot98_free(c);
	boot98_free(copy);
	CHECK(boot98_heap_current() == 0 && boot98_heap_validate());
	CHECK(boot98_heap_largest_free() == initial_largest);
	a = boot98_malloc(32);
	CHECK(a != NULL);
	boot98_free(a);
	boot98_free(a);
	CHECK(boot98_heap_error_count() == 1 && boot98_heap_validate());
	boot98_heap_reset();
	CHECK(boot98_heap_current() == 0 && boot98_heap_peak() == 0);
	CHECK(boot98_heap_error_count() == 0 && boot98_heap_validate());
	return 0;
}

static int
test_fault_injection(void)
{
	static unsigned char arena[4096];
	size_t fail_after;

	for (fail_after = 0; fail_after < 8; fail_after++) {
		void *items[8];
		size_t index;
		boot98_heap_init(arena, sizeof(arena));
		boot98_heap_set_failure_after(fail_after);
		for (index = 0; index < 8; index++)
			items[index] = boot98_malloc(24 + index);
		for (index = 0; index < fail_after; index++)
			CHECK(items[index] != NULL);
		for (index = fail_after; index < 8; index++)
			CHECK(items[index] == NULL);
		CHECK(boot98_heap_validate());
		for (index = 0; index < fail_after; index++)
			boot98_free(items[index]);
		CHECK(boot98_heap_current() == 0 && boot98_heap_validate());
	}
	return 0;
}

static int
test_heap_boundaries(void)
{
	static unsigned char arena[1024];
	unsigned char *first;
	void *guard;
	void *replacement;
	size_t index;

	boot98_heap_init(NULL, sizeof(arena));
	CHECK(boot98_malloc(1) == NULL && boot98_heap_validate());
	boot98_heap_init(arena, 1);
	CHECK(boot98_malloc(1) == NULL && boot98_heap_validate());

	boot98_heap_init(arena, sizeof(arena));
	first = boot98_malloc(64);
	guard = boot98_malloc(64);
	CHECK(first != NULL && guard != NULL);
	for (index = 0; index < 64; index++)
		first[index] = (unsigned char)index;
	boot98_heap_set_failure_after(0);
	replacement = boot98_realloc(first, 512);
	CHECK(replacement == NULL && boot98_heap_validate());
	for (index = 0; index < 64; index++)
		CHECK(first[index] == (unsigned char)index);
	boot98_free(first + 8);
	CHECK(boot98_heap_error_count() == 1 && boot98_heap_validate());
	boot98_free(first);
	boot98_free(guard);
	CHECK(boot98_heap_current() == 0 && boot98_heap_validate());
	return 0;
}

int
main(void)
{
	int result;
	result = test_memory_and_strings();
	if (result != 0) return result;
	result = test_ctype_and_numbers();
	if (result != 0) return result;
	result = test_integer_helpers();
	if (result != 0) return result;
	result = test_formatting();
	if (result != 0) return result;
	result = test_heap();
	if (result != 0) return result;
	result = test_fault_injection();
	if (result != 0) return result;
	result = test_heap_boundaries();
	if (result != 0) return result;
	return 0;
}
