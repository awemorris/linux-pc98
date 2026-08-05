/*
 * PC-9801-55/92 compatible WD33C93A SCSI host adapter driver
 *
 * Copyright (C) 1997-2003 Kyoto University Microcomputer Club
 *                            (Linux/98 project)
 *                            Tomoharu Ugawa <ohirune@kmc.gr.jp>
 * Copyright (C) 2026 Awe Morris
 *
 * This is a direct Linux 7.1 port of drivers/scsi/pc980155.c from the
 * Linux/98 2.6.7 tree.  The board-facing interrupt, DMA, reset and WD33C93
 * glue follows that historical driver.  Changes in the Awe Morris port are
 * limited to current SCSI APIs, PC-98 boot geometry, selectable 55/92 board
 * profiles and the low-memory bounce buffer which replaces the removed
 * unchecked_isa_dma SCSI host facility.
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <linux/delay.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/math64.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/string.h>

#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>

#include <asm/dma.h>
#include <asm/pc9800.h>

#include "wd33c93.h"
#include "pc980155.h"

#define DRV_NAME                        "pc9801-55-92"
#define PC980155_IO_SIZE                6
#define PC980155_DMA_BUFFER_SIZE        (64 * 1024)
#define PC980155_HOST_ID                7
#define PC980155_92_HEADS               8
#define PC980155_92_SECTORS             32
#define PC980155_55_HEADS               8
#define PC980155_55_SECTORS             17

#define PC9801_DMA_MASK                 0x0015
#define PC9801_DMA_MODE                 0x0017
#define PC9801_DMA_CLEAR_FF             0x0019

#ifndef CMD_PER_LUN
#define CMD_PER_LUN                     2
#endif
#ifndef CAN_QUEUE
#define CAN_QUEUE                       16
#endif

enum pc980155_profile {
	PC980155_PROFILE_92,
	PC980155_PROFILE_55,
};

struct pc980155_hostdata {
	/* wd33c93.c requires this to be the first member of host private data. */
	struct WD33C93_hostdata wh;
	void *dma_buffer;
	unsigned int dma_len;
	bool dma_dir_in;
};

static const unsigned int pc980155_default_ios[] = {
	0x0cc0, 0x0cd0, 0x0ce0, 0x0cf0,
};
static const u8 pc980155_irqs[] = { 3, 5, 6, 9, 12, 13 };

static struct Scsi_Host *pc980155_host;
static unsigned int io;
static int pc980155_irq_override = -1;
static int pc980155_dma_override = -1;
static u8 pc980155_clock = WD33C93_FS_8_10;
static enum pc980155_profile pc980155_profile = PC980155_PROFILE_92;
static bool pc980155_async_pio;

module_param(io, uint, 0444);
MODULE_PARM_DESC(io, "PC-9801-55/92 base I/O port (default: probe 0xcc0-0xcf0)");

static bool pc980155_irq_valid(unsigned int irq)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(pc980155_irqs); i++)
		if (pc980155_irqs[i] == irq)
			return true;
	return false;
}

static int __init pc980155_setup(char *value)
{
	char *option;

	option = strsep(&value, ",");
	if (!strcmp(option, "55")) {
		pc980155_profile = PC980155_PROFILE_55;
		/* WINnote98 and PC-9801-55 use fixed INT1/DMA0. */
		pc980155_irq_override = 5;
		pc980155_dma_override = 0;
		pc980155_clock = WD33C93_FS_12_15;
	} else if (!strcmp(option, "92")) {
		pc980155_profile = PC980155_PROFILE_92;
	} else {
		pr_warn(DRV_NAME ": unknown pc9801_scsi=%s; using 92 profile\n",
			option);
	}

	while ((option = strsep(&value, ",")) != NULL) {
		unsigned int setting;

		if (!strncmp(option, "irq=", 4)) {
			if (kstrtouint(option + 4, 0, &setting) ||
			    !pc980155_irq_valid(setting)) {
				pr_warn(DRV_NAME ": invalid %s\n", option);
				continue;
			}
			pc980155_irq_override = setting;
		} else if (!strncmp(option, "dma=", 4)) {
			if (kstrtouint(option + 4, 0, &setting) ||
			    (setting != 0 && setting != 2 && setting != 3)) {
				pr_warn(DRV_NAME ": invalid %s\n", option);
				continue;
			}
			pc980155_dma_override = setting;
		} else if (!strncmp(option, "clock=", 6)) {
			if (kstrtouint(option + 6, 0, &setting)) {
				pr_warn(DRV_NAME ": invalid %s\n", option);
				continue;
			}
			switch (setting) {
			case 8:
				pc980155_clock = WD33C93_FS_8_10;
				break;
			case 12:
				pc980155_clock = WD33C93_FS_12_15;
				break;
			case 16:
				pc980155_clock = WD33C93_FS_16_20;
				break;
			default:
				pr_warn(DRV_NAME ": unsupported %s\n", option);
			}
		} else if (!strcmp(option, "mode=async-pio")) {
			/*
			 * Diagnostic and compatibility path for adapters whose DMA or
			 * synchronous-transfer wiring is not yet known to work.  Data
			 * travels through the WD33C93 WD_DATA register at SASR/SCMD;
			 * this does not use QEMU's optional base+6 data port.
			 */
			pc980155_async_pio = true;
		} else if (!strcmp(option, "mode=dma")) {
			pc980155_async_pio = false;
		} else if (*option) {
			pr_warn(DRV_NAME ": unknown option %s\n", option);
		}
	}
	return 1;
}
__setup("pc9801_scsi=", pc980155_setup);

static inline wd33c93_regs pc980155_regs(unsigned long base_io)
{
	wd33c93_regs regs = {
		.SASR = (volatile unsigned char *)PC980155_REG_ADDRST(base_io),
		.SCMD = (volatile unsigned char *)PC980155_REG_CONTROL(base_io),
	};

	return regs;
}

static inline void pc980155_dma_enable(unsigned long base_io)
{
	outb(0x01, PC980155_REG_CWRITE(base_io));
}

static inline void pc980155_dma_disable(unsigned long base_io)
{
	outb(0x02, PC980155_REG_CWRITE(base_io));
}

static bool pc980155_test_port(wd33c93_regs regs)
{
	return inb(pc980155_reg_port(regs.SASR)) != 0xff;
}

static int pc980155_getconfig(unsigned long base_io, wd33c93_regs regs,
			      u8 *irq, u8 *dma, u8 *scsi_id)
{
	u8 result = read_pc980155_resetint(regs);
	u8 irq_index = (result >> 3) & 0x07;
	u8 board_dma = inb(PC980155_REG_STATRD(base_io)) & 0x03;

	*scsi_id = result & 0x07;
	if (pc980155_irq_override >= 0) {
		*irq = pc980155_irq_override;
	} else {
		if (irq_index >= ARRAY_SIZE(pc980155_irqs)) {
			pr_err(DRV_NAME ": impossible IRQ setting %u at %#lx\n",
			       irq_index, base_io);
			return -ENODEV;
		}
		*irq = pc980155_irqs[irq_index];
	}

	if (pc980155_dma_override >= 0)
		*dma = pc980155_dma_override;
	else
		*dma = board_dma;

	if (*dma == 1 || *dma > 3) {
		pr_err(DRV_NAME ": impossible DMA channel %u at %#lx\n",
		       *dma, base_io);
		return -ENODEV;
	}
	return 0;
}

/*
 * The modern x86 ISA-DMA helpers program the PC/AT I/O map.  Linux/98's
 * asm/dma.h programmed the PC-98 odd-port layout, so preserve that historical
 * operation explicitly while still using request_dma() for ownership.
 */
static void pc980155_program_dma(unsigned int channel, unsigned long address,
				 unsigned int count, unsigned int mode)
{
	static const u16 page_port[] = { 0x0027, 0x0021, 0x0023, 0x0025 };
	u16 address_port = 0x0001 + channel * 4;
	u16 count_port = address_port + 2;
	unsigned int terminal_count = count - 1;

	outb(channel | 0x04, PC9801_DMA_MASK);
	outb(0, PC9801_DMA_CLEAR_FF);
	outb(address, address_port);
	outb(address >> 8, address_port);
	outb(terminal_count, count_port);
	outb(terminal_count >> 8, count_port);
	outb(address >> 16, page_port[channel]);
	outb(mode | channel, PC9801_DMA_MODE);
	outb(channel, PC9801_DMA_MASK);
}

static void pc980155_mask_dma(unsigned int channel)
{
	outb(channel | 0x04, PC9801_DMA_MASK);
}

static unsigned int pc980155_transfer_residual(wd33c93_regs regs)
{
	unsigned int residual;

	outb(WD_TRANSFER_COUNT_MSB, pc980155_reg_port(regs.SASR));
	residual = inb(pc980155_reg_port(regs.SCMD)) << 16;
	residual |= inb(pc980155_reg_port(regs.SCMD)) << 8;
	residual |= inb(pc980155_reg_port(regs.SCMD));
	return residual;
}

static int pc980155_dma_setup(struct scsi_cmnd *cmd, int dir_in)
{
	struct Scsi_Host *host = cmd->device->host;
	struct pc980155_hostdata *hdata = shost_priv(host);
	struct scsi_pointer *scsi_pointer = WD33C93_scsi_pointer(cmd);
	unsigned int len = scsi_pointer->this_residual;
	unsigned long flags;
	unsigned int mode;

	if (!len || len > PC980155_DMA_BUFFER_SIZE)
		return 1;

	mode = 0x40 | (dir_in ? 0x04 : 0x08);
	if (!dir_in)
		memcpy(hdata->dma_buffer, scsi_pointer->ptr, len);

	hdata->dma_len = len;
	hdata->dma_dir_in = dir_in;
	hdata->wh.dma_dir = dir_in;

	flags = claim_dma_lock();
	pc980155_program_dma(host->dma_channel,
			       virt_to_phys(hdata->dma_buffer), len, mode);
	release_dma_lock(flags);
	pc980155_dma_enable(host->base);
	return 0;
}

static void pc980155_dma_stop(struct Scsi_Host *host,
			      struct scsi_cmnd *cmd, int status)
{
	struct pc980155_hostdata *hdata = shost_priv(host);
	unsigned long flags;

	pc980155_dma_disable(host->base);
	flags = claim_dma_lock();
	pc980155_mask_dma(host->dma_channel);
	release_dma_lock(flags);

	if (cmd && status && hdata->dma_dir_in && hdata->dma_len) {
		struct scsi_pointer *scsi_pointer = WD33C93_scsi_pointer(cmd);
		unsigned int residual;
		unsigned int transferred;

		/*
		 * The historical driver DMAed directly into scsi_pointer->ptr,
		 * so an early disconnect naturally changed only bytes which had
		 * reached memory.  Preserve that behaviour when using the modern
		 * low-memory bounce buffer: copy only the bytes already transferred.
		 * wd33c93_intr() reads the same counter again immediately afterwards.
		 */
		residual = pc980155_transfer_residual(hdata->wh.regs);
		transferred = residual < hdata->dma_len ?
			hdata->dma_len - residual : 0;
		memcpy(scsi_pointer->ptr, hdata->dma_buffer, transferred);
	}
	hdata->dma_len = 0;
}

static irqreturn_t pc980155_intr(int irq, void *dev_id)
{
	struct Scsi_Host *host = dev_id;
	unsigned long flags;

	if (!(inb(host->base) & ASR_INT))
		return IRQ_NONE;

	spin_lock_irqsave(host->host_lock, flags);
	wd33c93_intr(host);
	spin_unlock_irqrestore(host->host_lock, flags);
	return IRQ_HANDLED;
}

static int pc980155_bus_reset(struct scsi_cmnd *cmd)
{
	struct Scsi_Host *host = cmd->device->host;
	struct pc980155_hostdata *hdata = shost_priv(host);
	wd33c93_regs regs = hdata->wh.regs;

	pc980155_int_disable(regs);
	pc980155_assert_bus_reset(regs);
	udelay(50);
	pc980155_negate_bus_reset(regs);
	(void)inb(pc980155_reg_port(regs.SASR));
	(void)read_pc980155(regs, WD_SCSI_STATUS);
	pc980155_int_enable(regs);
	return wd33c93_host_reset(cmd);
}

static int pc980155_bios_param(struct scsi_device *sdev,
			       struct gendisk *disk, sector_t capacity,
			       int geometry[])
{
	unsigned int heads;
	unsigned int sectors;

	if (!pc9800_get_boot_disk_geometry_for(0xa0, sdev->id,
						 &heads, &sectors)) {
		if (pc980155_profile == PC980155_PROFILE_55) {
			heads = PC980155_55_HEADS;
			sectors = PC980155_55_SECTORS;
		} else {
			heads = PC980155_92_HEADS;
			sectors = PC980155_92_SECTORS;
		}
	}

	geometry[0] = heads;
	geometry[1] = sectors;
	geometry[2] = min_t(sector_t, div_u64(capacity, heads * sectors),
				U16_MAX);
	return 0;
}

static const struct scsi_host_template pc980155_template = {
	.module                 = THIS_MODULE,
	.name                   = "NEC PC-9801-55/92 compatible SCSI",
	.show_info              = wd33c93_show_info,
	.write_info             = wd33c93_write_info,
	.proc_name              = "PC_9801_55",
	.queuecommand           = wd33c93_queuecommand,
	.eh_abort_handler       = wd33c93_abort,
	.eh_bus_reset_handler   = pc980155_bus_reset,
	.eh_host_reset_handler  = wd33c93_host_reset,
	.bios_param             = pc980155_bios_param,
	.can_queue              = CAN_QUEUE,
	.this_id                = PC980155_HOST_ID,
	.sg_tablesize           = SG_ALL,
	.cmd_per_lun            = CMD_PER_LUN,
	.max_sectors            = PC980155_DMA_BUFFER_SIZE / 512,
	.cmd_size               = sizeof(struct scsi_pointer),
};

static int __init pc980155_init(void)
{
	unsigned int ios[ARRAY_SIZE(pc980155_default_ios)];
	unsigned int nr_ios;
	unsigned long base_io = 0;
	wd33c93_regs regs = { };
	struct pc980155_hostdata *hdata;
	u8 irq = 0, dma = 0, scsi_id = PC980155_HOST_ID;
	unsigned int i;
	int error = -ENODEV;

	if (io) {
		ios[0] = io;
		nr_ios = 1;
	} else {
		memcpy(ios, pc980155_default_ios, sizeof(ios));
		nr_ios = ARRAY_SIZE(ios);
	}

	for (i = 0; i < nr_ios; i++) {
		base_io = ios[i];
		regs = pc980155_regs(base_io);
		if (!request_region(base_io, PC980155_IO_SIZE, DRV_NAME))
			continue;
		if (pc980155_test_port(regs) &&
		    !pc980155_getconfig(base_io, regs, &irq, &dma, &scsi_id))
			break;
		release_region(base_io, PC980155_IO_SIZE);
	}
	if (i == nr_ios) {
		pr_info(DRV_NAME ": not found\n");
		return -ENODEV;
	}

	error = request_dma(dma, DRV_NAME);
	if (error) {
		pr_err(DRV_NAME ": unable to allocate DMA %u: %d\n", dma, error);
		goto err_region;
	}

	pc980155_host = scsi_host_alloc(&pc980155_template,
					 sizeof(struct pc980155_hostdata));
	if (!pc980155_host) {
		error = -ENOMEM;
		goto err_dma;
	}

	pc980155_host->this_id = scsi_id;
	pc980155_host->base = base_io;
	pc980155_host->n_io_port = PC980155_IO_SIZE;
	pc980155_host->irq = irq;
	pc980155_host->dma_channel = dma;

	hdata = shost_priv(pc980155_host);
	hdata->dma_buffer = (void *)__get_free_pages(
		GFP_KERNEL | GFP_DMA, get_order(PC980155_DMA_BUFFER_SIZE));
	if (!hdata->dma_buffer) {
		error = -ENOMEM;
		goto err_host;
	}
	hdata->wh.fast = 0;
	hdata->wh.dma_mode = CTRL_DMA;

	error = request_irq(irq, pc980155_intr, 0, DRV_NAME, pc980155_host);
	if (error) {
		pr_err(DRV_NAME ": unable to allocate IRQ %u: %d\n", irq, error);
		goto err_buffer;
	}

	/* Match the Linux/98 driver: enable the board and let wd33c93.c reset
	 * and initialise the controller before the SCSI mid-layer scans it.
	 */
	pc980155_dma_disable(base_io);
	pc980155_int_enable(regs);
	wd33c93_init(pc980155_host, regs, pc980155_dma_setup,
		      pc980155_dma_stop, pc980155_clock);
	if (pc980155_async_pio) {
		/*
		 * wd33c93_init() deliberately owns the generic defaults.  Override
		 * them before scsi_scan_host() starts the first command so every
		 * target negotiates an offset of zero and transfer_bytes() selects
		 * the standard WD33C93 polled-PIO path.
		 */
		hdata->wh.no_sync = 0xff;
		hdata->wh.no_dma = 1;
		for (i = 0; i < 8; i++)
			hdata->wh.sync_stat[i] = SS_UNSET;
	}

	error = scsi_add_host(pc980155_host, NULL);
	if (error)
		goto err_irq_enabled;

	pr_info(DRV_NAME ": I/O %#lx, IRQ %u, DMA %u, SCSI ID %u; %s profile, %s\n",
		base_io, irq, dma, scsi_id,
		pc980155_profile == PC980155_PROFILE_55 ? "55" : "92",
		pc980155_async_pio ? "asynchronous PIO" : "DMA");
	pr_info(DRV_NAME ": probing SCSI targets 0-6; absent targets may take several seconds\n");
	scsi_scan_host(pc980155_host);
	return 0;

err_irq_enabled:
	pc980155_int_disable(regs);
	free_irq(irq, pc980155_host);
err_buffer:
	free_pages((unsigned long)hdata->dma_buffer,
		   get_order(PC980155_DMA_BUFFER_SIZE));
err_host:
	scsi_host_put(pc980155_host);
	pc980155_host = NULL;
err_dma:
	free_dma(dma);
err_region:
	release_region(base_io, PC980155_IO_SIZE);
	return error;
}

static void __exit pc980155_exit(void)
{
	struct pc980155_hostdata *hdata;
	wd33c93_regs regs;

	if (!pc980155_host)
		return;

	hdata = shost_priv(pc980155_host);
	regs = hdata->wh.regs;
	pc980155_int_disable(regs);
	scsi_remove_host(pc980155_host);
	free_irq(pc980155_host->irq, pc980155_host);
	pc980155_dma_disable(pc980155_host->base);
	free_pages((unsigned long)hdata->dma_buffer,
		   get_order(PC980155_DMA_BUFFER_SIZE));
	free_dma(pc980155_host->dma_channel);
	release_region(pc980155_host->base, pc980155_host->n_io_port);
	scsi_host_put(pc980155_host);
	pc980155_host = NULL;
}

module_init(pc980155_init);
module_exit(pc980155_exit);

MODULE_AUTHOR("Tomoharu Ugawa <ohirune@kmc.gr.jp>");
MODULE_AUTHOR("Awe Morris");
MODULE_DESCRIPTION("PC-9801-55/92 compatible WD33C93A SCSI host adapter");
MODULE_LICENSE("GPL");
