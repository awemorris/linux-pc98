/* Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef BOOT98_ASSERT_H
#define BOOT98_ASSERT_H

#ifdef NDEBUG
#define assert(expression) ((void)0)
#else
void boot98_assert_fail(const char *expression, const char *file, int line);
#define assert(expression) \
	((expression) ? (void)0 : boot98_assert_fail(#expression, __FILE__, __LINE__))
#endif

#endif
