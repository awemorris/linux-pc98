/*
 * PC-98 Bootstrap Environment Noct M6 JIT verification program
 * Copyright (C) 2026 Awe Morris
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef BOOT98_NOCT_M6_SCRIPT_H
#define BOOT98_NOCT_M6_SCRIPT_H

#define BOOT98_NOCT_M6_SOURCE \
	"func helper(a, b) { return (a * 3 + b) % 17; } " \
	"func main() { " \
	"var array = [2, 5, 8]; " \
	"array[1] = helper(array[0], array[2]); " \
	"var dict = {value: array[1]}; " \
	"var sum = 0; " \
	"for (value in array) { sum = sum + value; } " \
	"var bits = ((5 & 3) | 8) ^ 2; " \
	"bits = (bits << 1) >> 1; " \
	"if (dict.value == 14 && sum == 24 && bits == 11) { " \
	"Console.write(\"JIT:\" + sum + \":\" + dict.value + \":\" + bits + " \
	"\":\" + (123.0f / 321.0f) + \":\" + (123.0lf / 321.0lf)); " \
	"} else { Console.write(\"JIT:BAD\"); } }"

#define BOOT98_NOCT_M6_OUTPUT \
	"JIT:24:14:11:0.3831776:0.383177570093458"

#endif
