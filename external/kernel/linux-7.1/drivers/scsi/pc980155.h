/*
 * PC-9801-55 SCSI host adapter register access
 *
 * Copyright (C) 1997-2003 Kyoto University Microcomputer Club
 *                            (Linux/98 project)
 *                            Tomoharu Ugawa <ohirune@kmc.gr.jp>
 * Copyright (C) 2026 Awe Morris
 *
 * Ported from Linux/98 2.6.7 to Linux 7.1 in the Awe Morris port.
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef __PC980155_H
#define __PC980155_H

#include <linux/io.h>

#include "wd33c93.h"

#define PC980155_REG_ADDRST(base)       (base)
#define PC980155_REG_CONTROL(base)      ((base) + 2)
#define PC980155_REG_CWRITE(base)       ((base) + 4)
#define PC980155_REG_STATRD(base)       ((base) + 4)

#define WD_MEMORYBANK                   0x30
#define WD_RESETINT                     0x33

static inline unsigned long pc980155_reg_port(volatile unsigned char *reg)
{
	return (unsigned long)reg;
}

static inline u8 read_pc980155(const wd33c93_regs regs, u8 reg_num)
{
	outb(reg_num, pc980155_reg_port(regs.SASR));
	return inb(pc980155_reg_port(regs.SCMD));
}

static inline void write_pc980155(const wd33c93_regs regs, u8 reg_num,
				  u8 value)
{
	outb(reg_num, pc980155_reg_port(regs.SASR));
	outb(value, pc980155_reg_port(regs.SCMD));
}

static inline void write_memorybank(const wd33c93_regs regs, u8 value)
{
	write_pc980155(regs, WD_MEMORYBANK, value);
}

#define read_pc980155_resetint(regs) \
	read_pc980155((regs), WD_RESETINT)
#define pc980155_int_enable(regs) \
	write_memorybank((regs), read_pc980155((regs), WD_MEMORYBANK) | 0x04)
#define pc980155_int_disable(regs) \
	write_memorybank((regs), read_pc980155((regs), WD_MEMORYBANK) & ~0x04)
#define pc980155_assert_bus_reset(regs) \
	write_memorybank((regs), read_pc980155((regs), WD_MEMORYBANK) | 0x02)
#define pc980155_negate_bus_reset(regs) \
	write_memorybank((regs), read_pc980155((regs), WD_MEMORYBANK) & ~0x02)

#endif /* __PC980155_H */
