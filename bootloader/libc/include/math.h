/* Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef BOOT98_MATH_H
#define BOOT98_MATH_H

typedef float float_t;
typedef double double_t;

#define INFINITY (__builtin_inff())
#define NAN (__builtin_nanf(""))
#define M_PI_2 1.57079632679489661923
#define isnan(value) __builtin_isnan(value)

double sin(double value);
double cos(double value);
double tan(double value);
double sqrt(double value);
float sinf(float value);
float cosf(float value);
float tanf(float value);
float sqrtf(float value);
double fmod(double value, double divisor);
double scalbn(double value, int exponent);
double floor(double value);
long double fabsl(long double value);
long double copysignl(long double magnitude, long double sign);
long double fmodl(long double value, long double divisor);
long double scalbnl(long double value, int exponent);

#endif
