// SPDX-License-Identifier: GPL-2.0
/*
 * NEC PC-9800 built-in PATA frontend.
 *
 * Copyright (C) 1997-2000 Linux/98 project,
 *                            Kyoto University Microcomputer Club.
 * Copyright (C) 2026 Awe Morris
 *
 * The task-file ports, two-byte register spacing, bank selector, control port,
 * and IRQ are from the last official Linux PC-9800 IDE driver.  Transfer and
 * error handling are delegated to the official Linux 7.1 libata SFF code.
 */

#include <linux/ata.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/libata.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/ata_platform.h>

#include <asm/pc9800.h>

#define PC98_ATA_COMMAND_BASE	0x0640
#define PC98_ATA_COMMAND_END	0x064e
#define PC98_ATA_CONTROL	0x074c
#define PC98_ATA_BANK_SELECT	0x0432
#define PC98_ATA_IRQ		9
#define PC98_ATA_PORT_SHIFT	1
#define PC98_ATA_NR_PORTS	2

struct pc98_pata_host {
	u8 bank_control;
};

static struct scsi_host_template pc98_pata_sht = {
	ATA_PIO_SHT("pata_pc9800"),
};

static int pc98_pata_bios_param(struct scsi_device *sdev,
				struct gendisk *disk, sector_t capacity,
				int geometry[])
{
	unsigned int heads = 8;
	unsigned int sectors = 17;

	/*
	 * The NEC98 partition table uses the BIOS logical geometry supplied by
	 * the loader. ATA commands remain LBA-first in libata; this callback is
	 * only the legacy geometry reported to upper layers.
	 */
	pc9800_get_boot_disk_geometry(&heads, &sectors);
	geometry[0] = heads;
	geometry[1] = sectors;
	sector_div(capacity, geometry[0] * geometry[1]);
	geometry[2] = capacity;
	return 0;
}

static void pc98_pata_select_bank(struct ata_port *ap)
{
	struct pc98_pata_host *hpriv = ap->host->private_data;

	outb(hpriv->bank_control | (ap->port_no & 1),
	     PC98_ATA_BANK_SELECT);
}

static void pc98_pata_dev_select(struct ata_port *ap, unsigned int device)
{
	pc98_pata_select_bank(ap);
	ata_sff_dev_select(ap, device);
}

static void pc98_pata_set_devctl(struct ata_port *ap, u8 ctl)
{
	pc98_pata_select_bank(ap);
	iowrite8(ctl, ap->ioaddr.ctl_addr);
}

static u8 pc98_pata_check_status(struct ata_port *ap)
{
	pc98_pata_select_bank(ap);
	return ata_sff_check_status(ap);
}

static u8 pc98_pata_check_altstatus(struct ata_port *ap)
{
	pc98_pata_select_bank(ap);
	return ioread8(ap->ioaddr.altstatus_addr);
}

static void pc98_pata_tf_load(struct ata_port *ap,
			      const struct ata_taskfile *tf)
{
	pc98_pata_select_bank(ap);
	ata_sff_tf_load(ap, tf);
}

static void pc98_pata_tf_read(struct ata_port *ap, struct ata_taskfile *tf)
{
	pc98_pata_select_bank(ap);
	ata_sff_tf_read(ap, tf);
}

static void pc98_pata_exec_command(struct ata_port *ap,
				   const struct ata_taskfile *tf)
{
	pc98_pata_select_bank(ap);
	ata_sff_exec_command(ap, tf);
}

static unsigned int pc98_pata_data_xfer(struct ata_queued_cmd *qc,
					unsigned char *buf,
					unsigned int buflen, int rw)
{
	pc98_pata_select_bank(qc->ap);
	return ata_sff_data_xfer(qc, buf, buflen, rw);
}

static int pc98_pata_set_mode(struct ata_link *link,
			      struct ata_device **unused)
{
	struct ata_device *dev;

	ata_for_each_dev(dev, link, ENABLED) {
		dev->pio_mode = dev->xfer_mode = XFER_PIO_0;
		dev->xfer_shift = ATA_SHIFT_PIO;
		dev->flags |= ATA_DFLAG_PIO;
		ata_dev_info(dev, "configured for PIO\n");
	}
	return 0;
}

static struct ata_port_operations pc98_pata_ops = {
	.inherits		= &ata_sff_port_ops,
	.cable_detect		= ata_cable_unknown,
	.set_mode		= pc98_pata_set_mode,
	.sff_dev_select		= pc98_pata_dev_select,
	.sff_set_devctl		= pc98_pata_set_devctl,
	.sff_check_status	= pc98_pata_check_status,
	.sff_check_altstatus	= pc98_pata_check_altstatus,
	.sff_tf_load		= pc98_pata_tf_load,
	.sff_tf_read		= pc98_pata_tf_read,
	.sff_exec_command	= pc98_pata_exec_command,
	.sff_data_xfer		= pc98_pata_data_xfer,
};

static void pc98_pata_setup_ioaddr(struct ata_ioports *ioaddr,
				   void __iomem *cmd, void __iomem *ctl)
{
	ioaddr->cmd_addr = cmd;
	ioaddr->ctl_addr = ctl;
	ioaddr->altstatus_addr = ctl;
	ioaddr->data_addr = cmd + (ATA_REG_DATA << PC98_ATA_PORT_SHIFT);
	ioaddr->error_addr = cmd + (ATA_REG_ERR << PC98_ATA_PORT_SHIFT);
	ioaddr->feature_addr = cmd + (ATA_REG_FEATURE << PC98_ATA_PORT_SHIFT);
	ioaddr->nsect_addr = cmd + (ATA_REG_NSECT << PC98_ATA_PORT_SHIFT);
	ioaddr->lbal_addr = cmd + (ATA_REG_LBAL << PC98_ATA_PORT_SHIFT);
	ioaddr->lbam_addr = cmd + (ATA_REG_LBAM << PC98_ATA_PORT_SHIFT);
	ioaddr->lbah_addr = cmd + (ATA_REG_LBAH << PC98_ATA_PORT_SHIFT);
	ioaddr->device_addr = cmd + (ATA_REG_DEVICE << PC98_ATA_PORT_SHIFT);
	ioaddr->status_addr = cmd + (ATA_REG_STATUS << PC98_ATA_PORT_SHIFT);
	ioaddr->command_addr = cmd + (ATA_REG_CMD << PC98_ATA_PORT_SHIFT);
}

static struct resource pc98_pata_resources[] = {
	{
		.start = PC98_ATA_COMMAND_BASE,
		.end = PC98_ATA_COMMAND_END,
		.flags = IORESOURCE_IO,
	},
	{
		.start = PC98_ATA_CONTROL,
		.end = PC98_ATA_CONTROL,
		.flags = IORESOURCE_IO,
	},
	{
		.start = PC98_ATA_BANK_SELECT,
		.end = PC98_ATA_BANK_SELECT,
		.flags = IORESOURCE_IO,
	},
	{
		.start = PC98_ATA_IRQ,
		.end = PC98_ATA_IRQ,
		.flags = IORESOURCE_IRQ,
	},
};

static int pc98_pata_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct pc98_pata_host *hpriv;
	struct ata_host *host;
	void __iomem *cmd, *ctl;
	int i;

	cmd = devm_ioport_map(dev, PC98_ATA_COMMAND_BASE,
			      PC98_ATA_COMMAND_END - PC98_ATA_COMMAND_BASE + 1);
	ctl = devm_ioport_map(dev, PC98_ATA_CONTROL, 1);
	if (!cmd || !ctl)
		return -ENOMEM;

	hpriv = devm_kzalloc(dev, sizeof(*hpriv), GFP_KERNEL);
	if (!hpriv)
		return -ENOMEM;

	/* Preserve all controller state except the bank-select bit. */
	hpriv->bank_control = inb(PC98_ATA_BANK_SELECT) & ~BIT(0);

	host = ata_host_alloc(dev, PC98_ATA_NR_PORTS);
	if (!host)
		return -ENOMEM;
	host->private_data = hpriv;

	for (i = 0; i < PC98_ATA_NR_PORTS; i++) {
		struct ata_port *ap = host->ports[i];

		ap->ops = &pc98_pata_ops;
		ap->pio_mask = ATA_PIO0;
		ap->flags |= ATA_FLAG_SLAVE_POSS;
		pc98_pata_setup_ioaddr(&ap->ioaddr, cmd, ctl);
		ata_port_desc(ap, "ioport cmd 0x%x ctl 0x%x bank %d",
			      PC98_ATA_COMMAND_BASE, PC98_ATA_CONTROL, i);
	}

	/* Leave the primary bank selected while libata starts probing. */
	outb(hpriv->bank_control, PC98_ATA_BANK_SELECT);

	return ata_host_activate(host, PC98_ATA_IRQ, ata_sff_interrupt,
				 IRQF_SHARED, &pc98_pata_sht);
}

static struct platform_driver pc98_pata_driver = {
	.probe = pc98_pata_probe,
	.remove = ata_platform_remove_one,
	.driver = {
		.name = "pata_pc9800",
	},
};
module_platform_driver(pc98_pata_driver);

static int __init pc98_pata_device_init(void)
{
	struct platform_device *device;

	pc98_pata_sht.bios_param = pc98_pata_bios_param;
	device = platform_device_register_simple("pata_pc9800",
						 PLATFORM_DEVID_NONE,
						 pc98_pata_resources,
						 ARRAY_SIZE(pc98_pata_resources));
	return PTR_ERR_OR_ZERO(device);
}
arch_initcall(pc98_pata_device_init);

MODULE_AUTHOR("PC-9800 Lovers");
MODULE_DESCRIPTION("NEC PC-9800 built-in PATA driver");
MODULE_LICENSE("GPL");
