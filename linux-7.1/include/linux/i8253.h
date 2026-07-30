/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 *  Machine specific IO port address definition for generic.
 *  Written by Osamu Tomita <tomita@cinet.co.jp>
 */
#ifndef __LINUX_I8253_H
#define __LINUX_I8253_H

#include <linux/param.h>
#include <linux/spinlock.h>
#include <linux/timex.h>

/* i8253A PIT registers */
#ifdef CONFIG_X86_PC9800
/* NEC PC-9800: 8253 counters at I/O 0x71/0x73/0x75, control at 0x77. */
#define PIT_MODE	0x77
#define PIT_CH0		0x71
#define PIT_CH2		0x75
#else
#define PIT_MODE	0x43
#define PIT_CH0		0x40
#define PIT_CH2		0x42
#endif

#define PIT_LATCH	((PIT_TICK_RATE + HZ/2) / HZ)

extern raw_spinlock_t i8253_lock;
extern struct clock_event_device i8253_clockevent;
extern void clockevent_i8253_init(bool oneshot);
extern void clockevent_i8253_disable(void);

extern void setup_pit_timer(void);

#endif /* __LINUX_I8253_H */
