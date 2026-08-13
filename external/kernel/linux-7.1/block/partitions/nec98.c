// SPDX-License-Identifier: GPL-2.0-only
/*
 * NEC PC-9800 fixed-disk partition table support
 *
 * The table occupies LBA 1 and contains sixteen packed 32-byte entries.
 * CHS fields are zero based.  Interpret them using the geometry reported by
 * the block device.  The boot loader's BIOS logical geometry is only a
 * compatibility fallback for drivers which do not provide one themselves.
 *
 * Copyright (C) 1999 Kyoto University Microcomputer Club
 * Copyright (C) 2026 Awe Morris
 */

#include <linux/hdreg.h>
#include <linux/string.h>
#include <linux/unaligned.h>

#include <asm/pc9800.h>

#include "check.h"

#define NEC98_PARTITION_SECTOR	1
#define NEC98_PARTITIONS	16
#define NEC98_ENTRY_SIZE	32
#define NEC98_IPL_MAGIC_OFFSET	4
#define NEC98_IPL_MAGIC		"IPL1"
struct nec98_partition {
	u8 mid;
	u8 sid;
	u8 reserved[2];
	u8 ipl_sector;
	u8 ipl_head;
	__le16 ipl_cylinder;
	u8 start_sector;
	u8 start_head;
	__le16 start_cylinder;
	u8 end_sector;
	u8 end_head;
	__le16 end_cylinder;
	u8 name[16];
} __packed;

static bool nec98_chs_valid(u8 head, u8 sector,
			    unsigned int heads, unsigned int sectors)
{
	return head < heads && sector < sectors;
}

static sector_t nec98_chs_to_lba(u16 cylinder, u8 head, u8 sector,
				 unsigned int heads, unsigned int sectors)
{
	return ((sector_t)cylinder * heads + head) * sectors + sector;
}

static bool nec98_disk_geometry(struct gendisk *disk, unsigned int *heads,
				unsigned int *sectors)
{
	struct hd_geometry geometry = { };

	if (!disk->fops || !disk->fops->getgeo ||
	    disk->fops->getgeo(disk, &geometry) ||
	    !geometry.heads || !geometry.sectors)
		return false;

	*heads = geometry.heads;
	*sectors = geometry.sectors;
	return true;
}

static void nec98_set_partition_name(struct parsed_partitions *state, int slot,
				     const struct nec98_partition *entry)
{
	struct partition_meta_info *info = &state->parts[slot].info;
	size_t length = sizeof(entry->name);

	while (length && (entry->name[length - 1] == ' ' ||
			  entry->name[length - 1] == '\0'))
		length--;
	if (!length)
		return;
	length = min(length, sizeof(info->volname) - 1);
	memcpy(info->volname, entry->name, length);
	info->volname[length] = '\0';
	state->parts[slot].has_info = true;
}

static bool nec98_table_valid(const struct nec98_partition *table,
			      unsigned int heads, unsigned int sectors,
			      sector_t capacity)
{
	sector_t starts[NEC98_PARTITIONS], ends[NEC98_PARTITIONS];
	bool found = false;
	int count = 0;
	int i, j;

	for (i = 0; i < NEC98_PARTITIONS; i++) {
		const struct nec98_partition *entry = &table[i];
		sector_t ipl, start, end;
		u16 ipl_cylinder, start_cylinder, end_cylinder;

		if (!entry->mid && !entry->sid)
			continue;
		if (!entry->mid || !entry->sid ||
		    get_unaligned_le16(entry->reserved))
			return false;
		ipl_cylinder = get_unaligned_le16(&entry->ipl_cylinder);
		start_cylinder = get_unaligned_le16(&entry->start_cylinder);
		end_cylinder = get_unaligned_le16(&entry->end_cylinder);
		if (!nec98_chs_valid(entry->ipl_head, entry->ipl_sector,
				       heads, sectors) ||
		    !nec98_chs_valid(entry->start_head, entry->start_sector,
				       heads, sectors) ||
		    !nec98_chs_valid(entry->end_head, entry->end_sector,
				       heads, sectors))
			return false;
		ipl = nec98_chs_to_lba(ipl_cylinder, entry->ipl_head,
					 entry->ipl_sector, heads, sectors);
		start = nec98_chs_to_lba(start_cylinder, entry->start_head,
					   entry->start_sector, heads, sectors);
		end = nec98_chs_to_lba(end_cylinder, entry->end_head,
					 entry->end_sector, heads, sectors);
		if (ipl > start || start > end || end >= capacity)
			return false;
		for (j = 0; j < count; j++)
			if (start <= ends[j] && starts[j] <= end)
				return false;
		starts[count] = start;
		ends[count] = end;
		count++;
		found = true;
	}

	return found;
}

int nec98_partition(struct parsed_partitions *state)
{
	const struct nec98_partition *entry;
	unsigned char *boot, *table;
	unsigned int heads, sectors;
	sector_t capacity = get_capacity(state->disk);
	Sector boot_sector, table_sector;
	bool has_ipl_magic, has_mbr_signature;
	int found = 0;
	int i;

	if (queue_logical_block_size(state->disk->queue) != 512)
		return 0;

	if (!nec98_disk_geometry(state->disk, &heads, &sectors)) {
		if (!pc9800_get_boot_disk_geometry(&heads, &sectors)) {
			/* Legacy direct boots used the NEC IDE default. */
			heads = 8;
			sectors = 17;
			pr_warn_once("NEC98: no disk geometry supplied; using 8/17\n");
		}
	}

	/*
	 * A native PC-98 disk normally omits the PC/AT 55 AA marker.  Some old
	 * linux-pc98 images and the PC/AT + PC-98 unified loader need it, though.
	 * IPL1 identifies those as this project's PC-98-aware IPL, so prefer the
	 * LBA 1 NEC98 table even when the PC/AT marker is also present.  Without
	 * IPL1, leave a 55 AA disk for the MSDOS parser below us.
	 */
	boot = read_part_sector(state, 0, &boot_sector);
	if (!boot)
		return -1;
	has_ipl_magic = !memcmp(boot + NEC98_IPL_MAGIC_OFFSET,
				NEC98_IPL_MAGIC, sizeof(NEC98_IPL_MAGIC) - 1);
	has_mbr_signature = get_unaligned_le16(boot + 510) == 0xaa55;
	put_dev_sector(boot_sector);
	if (has_mbr_signature && !has_ipl_magic)
		return 0;

	table = read_part_sector(state, NEC98_PARTITION_SECTOR, &table_sector);
	if (!table)
		return -1;
	if (!nec98_table_valid((const struct nec98_partition *)table,
			       heads, sectors, capacity)) {
		put_dev_sector(table_sector);
		return 0;
	}

	entry = (const struct nec98_partition *)table;
	for (i = 0; i < NEC98_PARTITIONS && i + 1 < state->limit;
	     i++, entry++) {
		sector_t start, end;
		u16 start_cylinder, end_cylinder;

		if (!entry->mid || !entry->sid)
			continue;

		start_cylinder = get_unaligned_le16(&entry->start_cylinder);
		end_cylinder = get_unaligned_le16(&entry->end_cylinder);
		if (!nec98_chs_valid(entry->start_head, entry->start_sector,
				       heads, sectors) ||
		    !nec98_chs_valid(entry->end_head, entry->end_sector,
				       heads, sectors))
			continue;

		start = nec98_chs_to_lba(start_cylinder, entry->start_head,
					 entry->start_sector, heads, sectors);
		end = nec98_chs_to_lba(end_cylinder, entry->end_head,
				       entry->end_sector, heads, sectors);
		if (end < start || start >= capacity)
			continue;
		if (end >= capacity)
			end = capacity - 1;

		put_partition(state, i + 1, start, end - start + 1);
		nec98_set_partition_name(state, i + 1, entry);
		found++;
	}

	put_dev_sector(table_sector);
	if (!found)
		return 0;

	seq_buf_puts(&state->pp_buf, " NEC98\n");
	return 1;
}
