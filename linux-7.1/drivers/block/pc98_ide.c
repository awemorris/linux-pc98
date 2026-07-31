// SPDX-License-Identifier: GPL-2.0-only
/*
 * Minimal block driver for the NEC PC-9800 built-in IDE interface.
 *
 * This is intentionally a small, fixed-configuration alternative to the
 * libata/SCSI-disk path for memory-constrained i386 machines.  It supports
 * one master ATA disk, 512-byte LBA28 PIO reads and writes, and cache flush.
 * The device interrupt is disabled and commands are polled synchronously.
 */

#include <linux/blk-mq.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <linux/highmem.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/module.h>

#define DRV_NAME		"pc98ide"
#define PC98IDE_DISK_NAME	"hd98a"
#define PC98IDE_MINORS		16

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
	bool flush_supported;
};

static struct pc98ide_device pc98ide;
static int pc98ide_major;

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

static int pc98ide_select_lba(u32 lba)
{
	int ret;

	ret = pc98ide_wait(false);
	if (ret)
		return ret;

	outb(ATA_DEV_LBA | ((lba >> 24) & 0x0f), PC98IDE_DEVICE);
	pc98ide_400ns_delay();

	outb(1, PC98IDE_NSECTOR);
	outb(lba, PC98IDE_LBAL);
	outb(lba >> 8, PC98IDE_LBAM);
	outb(lba >> 16, PC98IDE_LBAH);
	return 0;
}

static int pc98ide_rw_sector(sector_t sector, void *buffer, bool write)
{
	int ret;

	if (sector >= pc98ide.sectors || sector > 0x0fffffff)
		return -EIO;

	ret = pc98ide_select_lba((u32)sector);
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

static int pc98ide_flush(void)
{
	int ret;

	if (!pc98ide.flush_supported)
		return 0;

	ret = pc98ide_wait(false);
	if (ret)
		return ret;
	outb(ATA_DEV_LBA, PC98IDE_DEVICE);
	pc98ide_400ns_delay();
	outb(ATA_CMD_FLUSH, PC98IDE_COMMAND);
	return pc98ide_wait(false);
}

static blk_status_t pc98ide_queue_rq(struct blk_mq_hw_ctx *hctx,
				     const struct blk_mq_queue_data *bd)
{
	struct request *rq = bd->rq;
	struct req_iterator iter;
	struct bio_vec bvec;
	sector_t sector = blk_rq_pos(rq);
	blk_status_t status = BLK_STS_OK;
	bool write;

	/*
	 * The single hardware queue has depth one and this polling request is
	 * completed before queue_rq() returns, so blk-mq already serializes all
	 * access.  In particular, do not disable interrupts around slow PIO.
	 */
	blk_mq_start_request(rq);

	if (req_op(rq) == REQ_OP_FLUSH) {
		if (pc98ide_flush())
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
			if (pc98ide_rw_sector(sector++, buffer + offset, write)) {
				status = BLK_STS_IOERR;
				break;
			}
		}
		kunmap_local(mapping);
		if (status != BLK_STS_OK)
			break;
	}

	if (status == BLK_STS_OK && write && (rq->cmd_flags & REQ_FUA) &&
	    pc98ide_flush())
		status = BLK_STS_IOERR;

done:
	blk_mq_end_request(rq, status);
	return BLK_STS_OK;
}

static const struct blk_mq_ops pc98ide_mq_ops = {
	.queue_rq = pc98ide_queue_rq,
};

static const struct block_device_operations pc98ide_fops = {
	.owner = THIS_MODULE,
};

static int __init pc98ide_identify(void)
{
	u16 id[256];
	u32 sectors;
	int ret;

	outb(0, PC98IDE_BANK);
	outb(ATA_CTL_NIEN | ATA_CTL_SRST, PC98IDE_CONTROL);
	udelay(10);
	outb(ATA_CTL_NIEN, PC98IDE_CONTROL);
	udelay(2000);

	ret = pc98ide_wait(false);
	if (ret)
		return ret;

	outb(ATA_DEV_LBA, PC98IDE_DEVICE);
	pc98ide_400ns_delay();
	outb(ATA_CMD_IDENTIFY, PC98IDE_COMMAND);
	if (inb(PC98IDE_STATUS) == 0)
		return -ENODEV;
	ret = pc98ide_wait(true);
	if (ret)
		return ret;
	insw(PC98IDE_DATA, id, 256);

	if (!(id[49] & ATA_ID_LBA))
		return -ENODEV;
	sectors = (u32)id[ATA_ID_LBA_CAPACITY] |
		  ((u32)id[ATA_ID_LBA_CAPACITY + 1] << 16);
	if (!sectors)
		return -ENODEV;

	pc98ide.sectors = min_t(sector_t, sectors, 0x10000000ULL);
	pc98ide.flush_supported = id[ATA_ID_COMMAND_SET_1] & ATA_ID_FLUSH;
	return 0;
}

static void pc98ide_release_regions(void)
{
	release_region(PC98IDE_BANK, 1);
	release_region(PC98IDE_CONTROL, 1);
	release_region(PC98IDE_DATA, 16);
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
	int ret;

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

	ret = pc98ide_identify();
	if (ret)
		goto out_regions;

	pc98ide_major = register_blkdev(0, DRV_NAME);
	if (pc98ide_major < 0) {
		ret = pc98ide_major;
		goto out_regions;
	}

	ret = blk_mq_alloc_sq_tag_set(&pc98ide.tag_set, &pc98ide_mq_ops, 1, 0);
	if (ret)
		goto out_major;

	pc98ide.disk = blk_mq_alloc_disk(&pc98ide.tag_set, &limits, &pc98ide);
	if (IS_ERR(pc98ide.disk)) {
		ret = PTR_ERR(pc98ide.disk);
		goto out_tags;
	}

	pc98ide.disk->major = pc98ide_major;
	pc98ide.disk->first_minor = 0;
	pc98ide.disk->minors = PC98IDE_MINORS;
	pc98ide.disk->fops = &pc98ide_fops;
	pc98ide.disk->private_data = &pc98ide;
	strscpy(pc98ide.disk->disk_name, PC98IDE_DISK_NAME,
		sizeof(pc98ide.disk->disk_name));
	set_capacity(pc98ide.disk, pc98ide.sectors);

	ret = add_disk(pc98ide.disk);
	if (ret)
		goto out_disk;

	pr_info(DRV_NAME ": %s: %llu sectors (%llu MiB), polling PIO\n",
		PC98IDE_DISK_NAME, (unsigned long long)pc98ide.sectors,
		(unsigned long long)(pc98ide.sectors >> 11));
	return 0;

out_disk:
	put_disk(pc98ide.disk);
out_tags:
	blk_mq_free_tag_set(&pc98ide.tag_set);
out_major:
	unregister_blkdev(pc98ide_major, DRV_NAME);
out_regions:
	pc98ide_release_regions();
	return ret;
}

static void __exit pc98ide_exit(void)
{
	del_gendisk(pc98ide.disk);
	put_disk(pc98ide.disk);
	blk_mq_free_tag_set(&pc98ide.tag_set);
	unregister_blkdev(pc98ide_major, DRV_NAME);
	pc98ide_release_regions();
}

module_init(pc98ide_init);
module_exit(pc98ide_exit);

MODULE_DESCRIPTION("Minimal NEC PC-9800 IDE block driver");
MODULE_LICENSE("GPL");
