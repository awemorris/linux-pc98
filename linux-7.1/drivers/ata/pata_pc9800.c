// SPDX-License-Identifier: GPL-2.0
/*
 * NEC PC-9800 built-in PATA frontend.
 *
 * Copyright (C) 1997-2000 Linux/98 project,
 *                            Kyoto University Microcomputer Club.
 * Copyright (C) 2026 Awe Morris
 *
 * The task-file ports, two-byte register spacing, control port, and IRQ are
 * from the last official Linux PC-9800 IDE driver.  Transfer and error
 * handling are delegated to the official Linux 7.1 pata_platform/libata code.
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
		.start = PC98_ATA_IRQ,
		.end = PC98_ATA_IRQ,
		.flags = IORESOURCE_IRQ,
	},
};

static int pc98_pata_probe(struct platform_device *pdev)
{
	/*
	 * The official PC-98 IDE driver selects the built-in interface through
	 * port 0x432.  This frontend exposes only interface zero, so select and
	 * retain that bank for the lifetime of the device.
	 */
	if (!devm_request_region(&pdev->dev, PC98_ATA_BANK_SELECT, 1,
				 "pata_pc9800 bank"))
		return -EBUSY;
	outb(0, PC98_ATA_BANK_SELECT);

	return __pata_platform_probe(&pdev->dev, &pc98_pata_resources[0],
				     &pc98_pata_resources[1],
				     &pc98_pata_resources[2],
				     PC98_ATA_PORT_SHIFT, ATA_PIO0,
				     &pc98_pata_sht, true);
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
