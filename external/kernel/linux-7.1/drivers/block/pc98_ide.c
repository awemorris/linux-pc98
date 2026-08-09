// SPDX-License-Identifier: GPL-2.0-only
/*
 * Minimal block driver for the NEC PC-9800 built-in IDE interface.
 *
 * This is intentionally a small, fixed-configuration alternative to the
 * libata/SCSI-disk path for memory-constrained i386 machines.  It supports
 * the master and slave ATA disks, 512-byte LBA28/CHS PIO reads and writes,
 * and cache flush.  The device interrupt is disabled and commands are polled
 * synchronously.
 *
 * Hardware mapping derived from the Linux/98 project PC-9800 IDE driver.
 * Copyright (C) 1997-2000 Linux/98 project,
 *                            Kyoto University Microcomputer Club.
 * Copyright (C) 2026 Awe Morris
 */

#include <linux/blk-mq.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <linux/highmem.h>
#include <linux/hdreg.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/module.h>
#include <linux/spinlock.h>

#include <asm/pc9800.h>

#define DRV_NAME		"pc98ide"
#define PC98IDE_MINORS		16
#define PC98IDE_DEVICES		2

#define PC98IDE_BANK		0x0432
#define PC98IDE_DATA		0x0640
#define PC98IDE_ERROR		0x0642
#define PC98IDE_NSECTOR		0x0644
#define PC98IDE_LBAL		0x0646
#define PC98IDE_LBAM		0x0648
#define PC98IDE_LBAH		0x064a
#define PC98IDE_DEVICE		0x064c
#define PC98IDE_STATUS		0x064e
#define PC98IDE_COMMAND		0x064e
#define PC98IDE_ALTSTATUS	0x074c
#define PC98IDE_CONTROL		0x074c

#define ATA_SR_BSY		0x80
#define ATA_SR_DRDY		0x40
#define ATA_SR_DF		0x20
#define ATA_SR_DRQ		0x08
#define ATA_SR_ERR		0x01

#define ATA_DEV_LBA		0xe0
#define ATA_CTL_NIEN		0x02
#define ATA_CTL_SRST		0x04

#define ATA_CMD_READ		0x20
#define ATA_CMD_WRITE		0x30
#define ATA_CMD_FLUSH		0xe7
#define ATA_CMD_IDENTIFY	0xec

#define ATA_ID_LBA_CAPACITY	60
#define ATA_ID_COMMAND_SET_1	83
#define ATA_ID_LBA		(1U << 9)
#define ATA_ID_FLUSH		(1U << 12)

#define PC98IDE_POLL_LOOPS	500000
#define PC98IDE_MAX_SECTORS	8

struct pc98ide_device {
	struct blk_mq_tag_set tag_set;
	struct gendisk *disk;
	sector_t sectors;
	u16 cylinders;
	u8 heads;
	u8 sectors_per_track;
	bool lba_supported;
	bool flush_supported;
	u8 unit;
	bool present;
	bool registered;
};

static struct pc98ide_device pc98ide[PC98IDE_DEVICES];
static int pc98ide_major;
static DEFINE_SPINLOCK(pc98ide_lock);

static int pc98ide_wait(bool drq)
{
	unsigned int i;
	u8 status;

	for (i = 0; i < PC98IDE_POLL_LOOPS; i++) {
		status = inb(PC98IDE_ALTSTATUS);
		if (!(status & ATA_SR_BSY)) {
			if (status & (ATA_SR_DF | ATA_SR_ERR))
				return -EIO;
			if (!drq || (status & ATA_SR_DRQ))
				return 0;
		}
		udelay(10);
	}

	return -ETIMEDOUT;
}

static void pc98ide_400ns_delay(void)
{
	inb(PC98IDE_ALTSTATUS);
	inb(PC98IDE_ALTSTATUS);
	inb(PC98IDE_ALTSTATUS);
	inb(PC98IDE_ALTSTATUS);
}

static int pc98ide_select_sector(struct pc98ide_device *dev, u32 lba)
{
	u32 track, cylinder;
	u8 head, sector;
	int ret;

	ret = pc98ide_wait(false);
	if (ret)
		return ret;

	if (dev->lba_supported) {
		outb(ATA_DEV_LBA | (dev->unit << 4) |
		     ((lba >> 24) & 0x0f), PC98IDE_DEVICE);
		pc98ide_400ns_delay();
		outb(1, PC98IDE_NSECTOR);
		outb(lba, PC98IDE_LBAL);
		outb(lba >> 8, PC98IDE_LBAM);
		outb(lba >> 16, PC98IDE_LBAH);
		return 0;
	}

	track = lba / dev->sectors_per_track;
	sector = lba % dev->sectors_per_track + 1;
	head = track % dev->heads;
	cylinder = track / dev->heads;
	if (cylinder >= dev->cylinders)
		return -ERANGE;

	outb(0xa0 | (dev->unit << 4) | head, PC98IDE_DEVICE);
	pc98ide_400ns_delay();
	outb(1, PC98IDE_NSECTOR);
	outb(sector, PC98IDE_LBAL);
	outb(cylinder, PC98IDE_LBAM);
	outb(cylinder >> 8, PC98IDE_LBAH);
	return 0;
}

static int pc98ide_rw_sector(struct pc98ide_device *dev, sector_t sector,
			    void *buffer, bool write)
{
	int ret;

	if (sector >= dev->sectors ||
	    (dev->lba_supported && sector > 0x0fffffff))
		return -EIO;

	ret = pc98ide_select_sector(dev, (u32)sector);
	if (ret)
		return ret;

	outb(write ? ATA_CMD_WRITE : ATA_CMD_READ, PC98IDE_COMMAND);
	ret = pc98ide_wait(true);
	if (ret)
		return ret;

	if (write) {
		outsw(PC98IDE_DATA, buffer, 256);
		return pc98ide_wait(false);
	}

	insw(PC98IDE_DATA, buffer, 256);
	return 0;
}

static int pc98ide_flush(struct pc98ide_device *dev)
{
	int ret;

	if (!dev->flush_supported)
		return 0;

	ret = pc98ide_wait(false);
	if (ret)
		return ret;
	outb(ATA_DEV_LBA | (dev->unit << 4), PC98IDE_DEVICE);
	pc98ide_400ns_delay();
	outb(ATA_CMD_FLUSH, PC98IDE_COMMAND);
	return pc98ide_wait(false);
}

static blk_status_t pc98ide_queue_rq(struct blk_mq_hw_ctx *hctx,
				     const struct blk_mq_queue_data *bd)
{
	struct pc98ide_device *dev = hctx->queue->queuedata;
	struct request *rq = bd->rq;
	struct req_iterator iter;
	struct bio_vec bvec;
	sector_t sector = blk_rq_pos(rq);
	blk_status_t status = BLK_STS_OK;
	bool write;

	/* Each disk has a depth-one blk-mq queue, but both share one ATA bus. */
	if (!spin_trylock(&pc98ide_lock))
		return BLK_STS_RESOURCE;
	blk_mq_start_request(rq);

	if (req_op(rq) == REQ_OP_FLUSH) {
		if (pc98ide_flush(dev))
			status = BLK_STS_IOERR;
		goto done;
	}
	if (req_op(rq) != REQ_OP_READ && req_op(rq) != REQ_OP_WRITE) {
		status = BLK_STS_NOTSUPP;
		goto done;
	}

	write = req_op(rq) == REQ_OP_WRITE;
	rq_for_each_segment(bvec, rq, iter) {
		void *mapping;
		u8 *buffer;
		unsigned int offset;

		if (bvec.bv_len & 511) {
			status = BLK_STS_IOERR;
			break;
		}

		mapping = kmap_local_page(bvec.bv_page);
		buffer = (u8 *)mapping + bvec.bv_offset;
		for (offset = 0; offset < bvec.bv_len; offset += 512) {
			if (pc98ide_rw_sector(dev, sector++, buffer + offset,
					       write)) {
				status = BLK_STS_IOERR;
				break;
			}
		}
		kunmap_local(mapping);
		if (status != BLK_STS_OK)
			break;
	}

	if (status == BLK_STS_OK && write && (rq->cmd_flags & REQ_FUA) &&
	    pc98ide_flush(dev))
		status = BLK_STS_IOERR;

done:
	blk_mq_end_request(rq, status);
	spin_unlock(&pc98ide_lock);
	return BLK_STS_OK;
}

static const struct blk_mq_ops pc98ide_mq_ops = {
	.queue_rq = pc98ide_queue_rq,
};

static int pc98ide_getgeo(struct gendisk *disk, struct hd_geometry *geo)
{
	struct pc98ide_device *dev = disk->private_data;
	unsigned int heads = dev->heads ? dev->heads : 8;
	unsigned int sectors = dev->sectors_per_track ?
		dev->sectors_per_track : 17;
	sector_t cylinders = dev->sectors;

	pc9800_get_boot_disk_geometry_for(0x80, dev->unit, &heads, &sectors);
	sector_div(cylinders, heads * sectors);
	geo->heads = heads;
	geo->sectors = sectors;
	geo->cylinders = min_t(sector_t, cylinders, 0xffff);
	geo->start = 0;
	return 0;
}

static const struct block_device_operations pc98ide_fops = {
	.owner = THIS_MODULE,
	.getgeo = pc98ide_getgeo,
};

static int __init pc98ide_identify(struct pc98ide_device *dev)
{
	u16 id[256];
	u16 heads, sectors_per_track;
	u32 sectors;
	int ret;

	ret = pc98ide_wait(false);
	if (ret)
		return ret;

	outb(ATA_DEV_LBA | (dev->unit << 4), PC98IDE_DEVICE);
	pc98ide_400ns_delay();
	if (inb(PC98IDE_STATUS) == 0 || inb(PC98IDE_STATUS) == 0xff)
		return -ENODEV;
	outb(ATA_CMD_IDENTIFY, PC98IDE_COMMAND);
	if (inb(PC98IDE_STATUS) == 0)
		return -ENODEV;
	ret = pc98ide_wait(true);
	if (ret)
		return ret;
	insw(PC98IDE_DATA, id, 256);

	dev->lba_supported = !!(id[49] & ATA_ID_LBA);
	if (dev->lba_supported) {
		sectors = (u32)id[ATA_ID_LBA_CAPACITY] |
			  ((u32)id[ATA_ID_LBA_CAPACITY + 1] << 16);
		if (!sectors)
			return -ENODEV;
		dev->sectors = min_t(sector_t, sectors, 0x10000000ULL);
	} else {
		dev->cylinders = id[1];
		heads = id[3];
		sectors_per_track = id[6];
		if (!dev->cylinders || !heads || heads > 16 ||
		    !sectors_per_track || sectors_per_track > 255)
			return -ENODEV;
		dev->heads = heads;
		dev->sectors_per_track = sectors_per_track;
		dev->sectors = (sector_t)dev->cylinders *
			dev->heads * dev->sectors_per_track;
	}
	dev->flush_supported = id[ATA_ID_COMMAND_SET_1] & ATA_ID_FLUSH;
	dev->present = true;
	return 0;
}

static void pc98ide_release_regions(void)
{
	release_region(PC98IDE_BANK, 1);
	release_region(PC98IDE_CONTROL, 1);
	release_region(PC98IDE_DATA, 16);
}

static void pc98ide_unregister_device(struct pc98ide_device *dev)
{
	if (!dev->registered)
		return;
	del_gendisk(dev->disk);
	put_disk(dev->disk);
	blk_mq_free_tag_set(&dev->tag_set);
	dev->disk = NULL;
	dev->registered = false;
}

static int __init pc98ide_register_device(struct pc98ide_device *dev,
					  struct queue_limits *limits)
{
	char name[DISK_NAME_LEN];
	int ret;

	ret = blk_mq_alloc_sq_tag_set(&dev->tag_set, &pc98ide_mq_ops, 1, 0);
	if (ret)
		return ret;

	dev->disk = blk_mq_alloc_disk(&dev->tag_set, limits, dev);
	if (IS_ERR(dev->disk)) {
		ret = PTR_ERR(dev->disk);
		dev->disk = NULL;
		blk_mq_free_tag_set(&dev->tag_set);
		return ret;
	}

	snprintf(name, sizeof(name), "hd%c", 'a' + dev->unit);
	dev->disk->major = pc98ide_major;
	dev->disk->first_minor = dev->unit * PC98IDE_MINORS;
	dev->disk->minors = PC98IDE_MINORS;
	dev->disk->fops = &pc98ide_fops;
	dev->disk->private_data = dev;
	strscpy(dev->disk->disk_name, name, sizeof(dev->disk->disk_name));
	set_capacity(dev->disk, dev->sectors);

	ret = add_disk(dev->disk);
	if (ret) {
		put_disk(dev->disk);
		dev->disk = NULL;
		blk_mq_free_tag_set(&dev->tag_set);
		return ret;
	}
	dev->registered = true;

	pr_info(DRV_NAME ": %s: %llu sectors (%llu MiB), %s polling PIO\n",
		name, (unsigned long long)dev->sectors,
		(unsigned long long)(dev->sectors >> 11),
		dev->lba_supported ? "LBA28" : "CHS");
	return 0;
}

static int __init pc98ide_init(void)
{
	struct queue_limits limits = {
		.logical_block_size = 512,
		.physical_block_size = 512,
		.max_hw_sectors = PC98IDE_MAX_SECTORS,
		.max_segments = 1,
		.max_segment_size = PAGE_SIZE,
		.features = BLK_FEAT_WRITE_CACHE,
	};
	unsigned int found = 0;
	int i, ret;

	if (!request_region(PC98IDE_DATA, 16, DRV_NAME))
		return -EBUSY;
	if (!request_region(PC98IDE_CONTROL, 1, DRV_NAME)) {
		release_region(PC98IDE_DATA, 16);
		return -EBUSY;
	}
	if (!request_region(PC98IDE_BANK, 1, DRV_NAME)) {
		release_region(PC98IDE_CONTROL, 1);
		release_region(PC98IDE_DATA, 16);
		return -EBUSY;
	}

	/* Reset both devices once, then identify master and slave separately. */
	outb(0, PC98IDE_BANK);
	outb(ATA_CTL_NIEN | ATA_CTL_SRST, PC98IDE_CONTROL);
	udelay(10);
	outb(ATA_CTL_NIEN, PC98IDE_CONTROL);
	udelay(2000);

	for (i = 0; i < PC98IDE_DEVICES; i++) {
		pc98ide[i].unit = i;
		if (!pc98ide_identify(&pc98ide[i]))
			found++;
	}
	if (!found) {
		ret = -ENODEV;
		goto out_regions;
	}

	pc98ide_major = register_blkdev(0, DRV_NAME);
	if (pc98ide_major < 0) {
		ret = pc98ide_major;
		goto out_regions;
	}

	for (i = 0; i < PC98IDE_DEVICES; i++) {
		if (!pc98ide[i].present)
			continue;
		ret = pc98ide_register_device(&pc98ide[i], &limits);
		if (ret)
			goto out_devices;
	}
	return 0;

out_devices:
	while (--i >= 0)
		pc98ide_unregister_device(&pc98ide[i]);
	unregister_blkdev(pc98ide_major, DRV_NAME);
out_regions:
	pc98ide_release_regions();
	return ret;
}

static void __exit pc98ide_exit(void)
{
	int i;

	for (i = PC98IDE_DEVICES - 1; i >= 0; i--)
		pc98ide_unregister_device(&pc98ide[i]);
	unregister_blkdev(pc98ide_major, DRV_NAME);
	pc98ide_release_regions();
}

module_init(pc98ide_init);
module_exit(pc98ide_exit);

MODULE_DESCRIPTION("Minimal NEC PC-9800 IDE block driver");
MODULE_LICENSE("GPL");
