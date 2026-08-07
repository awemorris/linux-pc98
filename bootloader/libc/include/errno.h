/* Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef BOOT98_ERRNO_H
#define BOOT98_ERRNO_H

extern int boot98_errno;
#define errno boot98_errno

#define EDOM 1
#define ERANGE 2
#define EINVAL 3
#define ENOMEM 4
#define EIO 5
#define ENOENT 6
#define EINTR 7

#endif
