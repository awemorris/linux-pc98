// SPDX-License-Identifier: GPL-2.0
/*
 * IDE (PATA) support for the NEC PC-9800's built-in interface.
 *
 * The taskfile is the usual ATA one, but at PC-98 addresses and with the
 * registers two bytes apart: data 0x640, error/feature 0x642, ... command
 * 0x64E, and device control / alternate status at 0x74C. Port 0x432 selects
 * which of the two interfaces the taskfile window talks to; the built-in
 * drives are on interface 0, so it is set once here.
 *
 * The data register is 16 bits wide only, hence use16bit — a 32-bit access
 * would spill into the error register.
 *
 * Everything else is the generic platform PATA path, so this driver is just
 * the PC-98 addressing plus that bank write.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/io.h>
#include <scsi/scsi_host.h>
#include <linux/libata.h>
#include <linux/platform_device.h>
#include <linux/ata_platform.h>

#define DRV_NAME	"pata_pc9800"

#define PC98_IDE_BANK	0x432		/* interface select */
#define PC98_IDE_CMD	0x640		/* taskfile base, registers 2 bytes apart */
#define PC98_IDE_CTL	0x74c		/* device control / altstatus */
#define PC98_IDE_IRQ	9

static int poll;
module_param(poll, int, 0);
MODULE_PARM_DESC(poll, "Use PIO polling instead of IRQ 9");

static const struct scsi_host_template pc9800_sht = {
	ATA_PIO_SHT(DRV_NAME),
};

static struct resource pc9800_res[] = {
	{ .start = PC98_IDE_CMD, .end = PC98_IDE_CMD + 15, .flags = IORESOURCE_IO },
	{ .start = PC98_IDE_CTL, .end = PC98_IDE_CTL,      .flags = IORESOURCE_IO },
	{ .start = PC98_IDE_IRQ, .end = PC98_IDE_IRQ,      .flags = IORESOURCE_IRQ },
};

static int pc9800_ide_probe(struct platform_device *pdev)
{
	outb(0, PC98_IDE_BANK);		/* talk to the built-in interface */

	return __pata_platform_probe(&pdev->dev, &pc9800_res[0], &pc9800_res[1],
				     poll ? NULL : &pc9800_res[2],
				     1 /* registers are 2 bytes apart */,
				     ATA_PIO4, &pc9800_sht, true /* 16-bit data */);
}

static struct platform_driver pc9800_ide_driver = {
	.probe	= pc9800_ide_probe,
	.driver	= {
		.name = DRV_NAME,
	},
};

static struct platform_device *pc9800_ide_pdev;

static int __init pc9800_ide_init(void)
{
	int ret;

	ret = platform_driver_register(&pc9800_ide_driver);
	if (ret)
		return ret;

	pc9800_ide_pdev = platform_device_register_simple(DRV_NAME, -1, NULL, 0);
	if (IS_ERR(pc9800_ide_pdev)) {
		platform_driver_unregister(&pc9800_ide_driver);
		return PTR_ERR(pc9800_ide_pdev);
	}
	return 0;
}

static void __exit pc9800_ide_exit(void)
{
	platform_device_unregister(pc9800_ide_pdev);
	platform_driver_unregister(&pc9800_ide_driver);
}

module_init(pc9800_ide_init);
module_exit(pc9800_ide_exit);

MODULE_DESCRIPTION("NEC PC-9800 built-in IDE driver");
MODULE_LICENSE("GPL");
