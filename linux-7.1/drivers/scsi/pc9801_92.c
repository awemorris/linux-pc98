/*
 * PC-9801-55/92 compatible WD33C93A SCSI host adapter
 * Copyright (C) 2026 Awe Morris
 *
 * Configuration register decoding is derived from the Linux/98
 * PC-9801-55 driver:
 * Copyright (C) 1997-2003 Kyoto University Microcomputer Club
 *                            (Linux/98 project)
 *                            Tomoharu Ugawa <ohirune@kmc.gr.jp>
 *
 * This first implementation deliberately uses the WD33C93A's combined
 * select-and-transfer command and programmed I/O.  The same transaction
 * sequence is used by the free PC-9801-92 option ROM in qemu-pc98.  Keeping
 * the driver single-command and polled makes it useful as a dependable
 * baseline on physical PC-9801-55/92 compatible C-Bus boards (including
 * Q-Vision WINnote98) and qemu-pc98.  The board configuration registers
 * are common to both families, so they deliberately use one driver.
 * Interrupt-driven DMA can be added without changing the SCSI-facing
 * interface later.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <linux/delay.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/scatterlist.h>
#include <linux/string.h>

#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>

#include <asm/pc9800.h>

#include "wd33c93.h"

#define DRV_NAME		"pc9801-55-92"

#define PC9801_92_IO		0x0cc0
#define PC9801_92_IOSIZE	8
#define PC9801_92_ASR		(PC9801_92_IO + 0)
#define PC9801_92_INDIRECT	(PC9801_92_IO + 2)
#define PC9801_92_DMA_CONTROL	(PC9801_92_IO + 4)
#define PC9801_92_DATA		(PC9801_92_IO + 6)

#define PC9801_92_DMA_DISABLE	0x02
#define PC9801_92_HOST_ID	7
#define PC9801_92_BIOS_HEADS	8
#define PC9801_92_BIOS_SECTORS	32
#define PC9801_55_BIOS_HEADS	8
#define PC9801_55_BIOS_SECTORS	17
#define PC9801_92_TIMEOUT_US	5000000
#define PC9801_92_BOARD_MEM_BANK	0x30
#define PC9801_92_BOARD_AUX_CONFIG	0x33
#define PC9801_92_BOARD_IRQ_ENABLE	0x04

static const u8 pc9801_55_92_irqs[] = { 3, 5, 6, 9, 12, 13 };

static struct Scsi_Host *pc9801_92_host;
static unsigned int pc9801_92_irq;
static unsigned int pc9801_92_dma;
static unsigned int pc9801_92_host_id = PC9801_92_HOST_ID;

enum pc9801_scsi_profile {
	PC9801_SCSI_PROFILE_92,
	PC9801_SCSI_PROFILE_55,
};

static enum pc9801_scsi_profile pc9801_scsi_profile =
	PC9801_SCSI_PROFILE_92;

static int __init pc9801_scsi_setup(char *value)
{
	if (!strcmp(value, "55"))
		pc9801_scsi_profile = PC9801_SCSI_PROFILE_55;
	else if (!strcmp(value, "92"))
		pc9801_scsi_profile = PC9801_SCSI_PROFILE_92;
	else
		pr_warn(DRV_NAME ": unknown pc9801_scsi=%s; using 92 profile\n",
			value);
	return 1;
}
__setup("pc9801_scsi=", pc9801_scsi_setup);

static inline u8 pc9801_92_read_asr(void)
{
	return inb(PC9801_92_ASR);
}

static inline void pc9801_92_select_reg(u8 reg)
{
	outb(reg, PC9801_92_ASR);
}

static inline u8 pc9801_92_read_reg(u8 reg)
{
	pc9801_92_select_reg(reg);
	return inb(PC9801_92_INDIRECT);
}

static inline void pc9801_92_write_reg(u8 reg, u8 value)
{
	pc9801_92_select_reg(reg);
	outb(value, PC9801_92_INDIRECT);
}

static int pc9801_55_92_read_config(void)
{
	u8 aux = pc9801_92_read_reg(PC9801_92_BOARD_AUX_CONFIG);
	u8 irq_index = (aux >> 3) & 7;
	u8 dma = inb(PC9801_92_DMA_CONTROL) & 3;

	if (irq_index >= ARRAY_SIZE(pc9801_55_92_irqs))
		return -ENODEV;

	/* DMA channel 1 is reserved by the PC-98 platform. */
	if (dma == 1)
		return -ENODEV;

	pc9801_92_host_id = aux & 7;
	pc9801_92_irq = pc9801_55_92_irqs[irq_index];
	pc9801_92_dma = dma;
	return 0;
}

static irqreturn_t pc9801_55_92_interrupt(int irq, void *dev_id)
{
	u8 asr = pc9801_92_read_asr();

	if (!(asr & ASR_INT))
		return IRQ_NONE;

	/*
	 * Transfers are polled with the board interrupt masked.  This handler
	 * consumes a completion left asserted by the option ROM before Linux
	 * takes ownership, which otherwise becomes an unhandled INT1/IRQ5 on
	 * fixed-configuration PC-9801-55 compatible boards such as WINnote98.
	 */
	(void)pc9801_92_read_reg(WD_SCSI_STATUS);
	return IRQ_HANDLED;
}

static int pc9801_92_wait(u8 mask, u8 *asr)
{
	unsigned int timeout = PC9801_92_TIMEOUT_US;
	u8 value;

	do {
		value = pc9801_92_read_asr();
		if (value & mask) {
			*asr = value;
			return 0;
		}
		udelay(1);
	} while (--timeout);

	*asr = value;
	return -ETIMEDOUT;
}

static int pc9801_92_reset_controller(void)
{
	u8 asr;
	u8 csr;

	/* The first release is strictly PIO, irrespective of firmware state. */
	outb(PC9801_92_DMA_DISABLE, PC9801_92_DMA_CONTROL);
	pc9801_92_write_reg(PC9801_92_BOARD_MEM_BANK,
		pc9801_92_read_reg(PC9801_92_BOARD_MEM_BANK) &
		~PC9801_92_BOARD_IRQ_ENABLE);
	pc9801_92_write_reg(WD_OWN_ID, OWNID_EAF | OWNID_RAF |
				 pc9801_92_host_id | OWNID_FS_8);
	pc9801_92_write_reg(WD_CONTROL, CTRL_IDI | CTRL_EDI | CTRL_POLLED);
	pc9801_92_write_reg(WD_COMMAND, WD_CMD_RESET);

	if (pc9801_92_wait(ASR_INT, &asr))
		return -ETIMEDOUT;

	csr = pc9801_92_read_reg(WD_SCSI_STATUS);
	if (csr != 0x00 && csr != 0x01)
		return -EIO;

	pc9801_92_write_reg(WD_TIMEOUT_PERIOD, TIMEOUT_PERIOD_VALUE);
	pc9801_92_write_reg(WD_SYNCHRONOUS_TRANSFER, 0);
	return 0;
}

static void pc9801_92_set_count(unsigned int count)
{
	pc9801_92_select_reg(WD_TRANSFER_COUNT_MSB);
	outb(count >> 16, PC9801_92_INDIRECT);
	outb(count >> 8, PC9801_92_INDIRECT);
	outb(count, PC9801_92_INDIRECT);
}

static void pc9801_92_write_cdb(const struct scsi_cmnd *cmd)
{
	unsigned int i;

	pc9801_92_select_reg(WD_CDB_1);
	for (i = 0; i < cmd->cmd_len; i++)
		outb(cmd->cmnd[i], PC9801_92_INDIRECT);
}

static int pc9801_92_transfer_data(struct scsi_cmnd *cmd)
{
	struct sg_mapping_iter miter;
	unsigned int flags;
	unsigned int transferred = 0;
	u8 asr;
	int error = 0;

	if (!scsi_bufflen(cmd))
		return 0;

	if (cmd->sc_data_direction == DMA_FROM_DEVICE)
		flags = SG_MITER_ATOMIC | SG_MITER_TO_SG;
	else if (cmd->sc_data_direction == DMA_TO_DEVICE)
		flags = SG_MITER_ATOMIC | SG_MITER_FROM_SG;
	else
		return -EINVAL;

	sg_miter_start(&miter, scsi_sglist(cmd), scsi_sg_count(cmd), flags);
	while (sg_miter_next(&miter)) {
		u8 *buffer = miter.addr;
		unsigned int i;

		for (i = 0; i < miter.length &&
		     transferred < scsi_bufflen(cmd); i++, transferred++) {
			error = pc9801_92_wait(ASR_DBR | ASR_INT, &asr);
			if (error) {
				miter.consumed = i;
				goto out;
			}
			if (!(asr & ASR_DBR)) {
				/* A short data phase may finish with CHECK CONDITION. */
				miter.consumed = i;
				scsi_set_resid(cmd,
					       scsi_bufflen(cmd) - transferred);
				goto out;
			}
			if (cmd->sc_data_direction == DMA_FROM_DEVICE)
				buffer[i] = inb(PC9801_92_DATA);
			else
				outb(buffer[i], PC9801_92_DATA);
		}
		miter.consumed = i;
	}

	if (transferred != scsi_bufflen(cmd))
		error = -EIO;
out:
	sg_miter_stop(&miter);
	if (error)
		scsi_set_resid(cmd, scsi_bufflen(cmd) - transferred);
	return error;
}

static int pc9801_92_execute(struct scsi_cmnd *cmd, u8 *status)
{
	unsigned int data_len = scsi_bufflen(cmd);
	u8 asr;
	u8 csr;
	int error;

	if (cmd->device->id == pc9801_92_host_id || cmd->device->id > 7 ||
	    cmd->device->lun > 7 ||
	    cmd->cmd_len > 12 || data_len > 0x00ffffff)
		return -EINVAL;

	/* Do not overwrite an unread completion from a preceding command. */
	if (pc9801_92_read_asr() & ASR_INT)
		pc9801_92_read_reg(WD_SCSI_STATUS);

	pc9801_92_write_reg(WD_DESTINATION_ID, cmd->device->id & 7);
	pc9801_92_write_reg(WD_TARGET_LUN, cmd->device->lun & 7);
	pc9801_92_set_count(data_len);
	pc9801_92_write_cdb(cmd);
	pc9801_92_write_reg(WD_COMMAND, WD_CMD_SEL_XFER);

	error = pc9801_92_transfer_data(cmd);
	if (error)
		return error;

	error = pc9801_92_wait(ASR_INT, &asr);
	if (error)
		return error;

	csr = pc9801_92_read_reg(WD_SCSI_STATUS);
	if (csr == CSR_TIMEOUT)
		return -ENODEV;
	if (csr != CSR_SEL_XFER_DONE)
		return -EIO;

	*status = pc9801_92_read_reg(WD_TARGET_LUN);
	return 0;
}

static enum scsi_qc_status pc9801_92_queuecommand(struct Scsi_Host *host,
						   struct scsi_cmnd *cmd)
{
	u8 status = 0;
	int error;

	cmd->result = 0;
	scsi_set_resid(cmd, 0);
	error = pc9801_92_execute(cmd, &status);
	if (!error)
		set_status_byte(cmd, status);
	else if (error == -ENODEV)
		set_host_byte(cmd, DID_NO_CONNECT);
	else if (error == -ETIMEDOUT)
		set_host_byte(cmd, DID_TIME_OUT);
	else
		set_host_byte(cmd, DID_ERROR);
	scsi_done(cmd);
	return 0;
}

static int pc9801_92_host_reset(struct scsi_cmnd *cmd)
{
	return pc9801_92_reset_controller() ? FAILED : SUCCESS;
}

static int pc9801_92_bios_param(struct scsi_device *sdev,
				struct gendisk *disk, sector_t capacity,
				int geometry[])
{
	unsigned int heads;
	unsigned int sectors;

	/*
	 * BOOT98 and LINUX98.EXE pass the BIOS SENSE geometry in
	 * SETUP_PC98_DISK.  It is authoritative for the boot disk because it
	 * is also the geometry used to encode the NEC98 partition table.
	 */
	if (!pc9800_get_boot_disk_geometry_for(0xa0, sdev->id,
						 &heads, &sectors)) {
		if (pc9801_scsi_profile == PC9801_SCSI_PROFILE_55) {
			heads = PC9801_55_BIOS_HEADS;
			sectors = PC9801_55_BIOS_SECTORS;
		} else {
			heads = PC9801_92_BIOS_HEADS;
			sectors = PC9801_92_BIOS_SECTORS;
		}
	}

	geometry[0] = heads;
	geometry[1] = sectors;
	geometry[2] = min_t(sector_t,
		div_u64(capacity, heads * sectors),
		U16_MAX);
	return 0;
}

static const struct scsi_host_template pc9801_92_template = {
	.module			= THIS_MODULE,
	.proc_name		= DRV_NAME,
	.name			= "NEC PC-9801-55/92 compatible SCSI",
	.queuecommand		= pc9801_92_queuecommand,
	.eh_host_reset_handler	= pc9801_92_host_reset,
	.bios_param		= pc9801_92_bios_param,
	.can_queue		= 1,
	.this_id		= PC9801_92_HOST_ID,
	.sg_tablesize		= SG_ALL,
	.cmd_per_lun		= 1,
};

static int __init pc9801_92_init(void)
{
	int error;

	if (!request_region(PC9801_92_IO, PC9801_92_IOSIZE, DRV_NAME))
		return -EBUSY;

	/* An unpopulated C-Bus I/O range reads as all ones. */
	if (pc9801_92_read_asr() == 0xff) {
		error = -ENODEV;
		goto err_release_region;
	}
	pr_info(DRV_NAME ": probing controller at I/O 0x%x...\n",
		PC9801_92_IO);

	error = pc9801_55_92_read_config();
	if (error) {
		pr_err(DRV_NAME ": invalid board configuration at I/O 0x%x\n",
		       PC9801_92_IO);
		goto err_release_region;
	}

	error = request_irq(pc9801_92_irq, pc9801_55_92_interrupt, 0,
			    DRV_NAME, &pc9801_92_host);
	if (error) {
		pr_err(DRV_NAME ": unable to claim IRQ %u: %d\n",
		       pc9801_92_irq, error);
		goto err_release_region;
	}

	pc9801_92_host = scsi_host_alloc(&pc9801_92_template, 0);
	if (!pc9801_92_host) {
		error = -ENOMEM;
		goto err_free_irq;
	}

	pc9801_92_host->base = PC9801_92_IO;
	pc9801_92_host->irq = pc9801_92_irq;
	pc9801_92_host->dma_channel = pc9801_92_dma;
	pc9801_92_host->this_id = pc9801_92_host_id;

	error = pc9801_92_reset_controller();
	if (error)
		goto err_put_host;

	error = scsi_add_host(pc9801_92_host, NULL);
	if (error)
		goto err_put_host;

	pr_info(DRV_NAME ": I/O 0x%x, IRQ %u, DMA %u, SCSI ID %u; "
		"%s profile, polled PIO mode\n", PC9801_92_IO,
		pc9801_92_irq, pc9801_92_dma, pc9801_92_host_id,
		pc9801_scsi_profile == PC9801_SCSI_PROFILE_55 ? "55" : "92");
	pr_info(DRV_NAME ": probing SCSI targets 0-%u; "
		"absent targets may take several seconds\n",
		PC9801_92_HOST_ID - 1);
	scsi_scan_host(pc9801_92_host);
	return 0;

err_put_host:
	scsi_host_put(pc9801_92_host);
	pc9801_92_host = NULL;
err_free_irq:
	free_irq(pc9801_92_irq, &pc9801_92_host);
err_release_region:
	release_region(PC9801_92_IO, PC9801_92_IOSIZE);
	return error;
}

static void __exit pc9801_92_exit(void)
{
	if (!pc9801_92_host)
		return;

	scsi_remove_host(pc9801_92_host);
	scsi_host_put(pc9801_92_host);
	free_irq(pc9801_92_irq, &pc9801_92_host);
	release_region(PC9801_92_IO, PC9801_92_IOSIZE);
	pc9801_92_host = NULL;
}

module_init(pc9801_92_init);
module_exit(pc9801_92_exit);

MODULE_AUTHOR("Awe Morris");
MODULE_DESCRIPTION("NEC PC-9801-55/92 compatible WD33C93A SCSI host adapter");
MODULE_LICENSE("GPL");
