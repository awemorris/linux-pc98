// SPDX-License-Identifier: GPL-2.0-only
/*
 * NEC PC-9800 fixed-disk partition table support
 *
 * The table occupies LBA 1 and contains sixteen packed 32-byte entries.
 * CHS fields are zero based.  The geometry is the BIOS logical geometry
 * passed by the boot loader, not ATA IDENTIFY geometry.  The two can differ
 * on real machines and on systems using an IPL-resident BIOS extension.
 */

#include <linux/unaligned.h>

#include <asm/pc9800.h>

#include "check.h"

#define NEC98_PARTITION_SECTOR	1
#define NEC98_PARTITIONS	16
#define NEC98_ENTRY_SIZE	32
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

static bool nec98_table_valid(const struct nec98_partition *table,
			      unsigned int heads, unsigned int sectors)
{
	bool found = false;
	int i;

	for (i = 0; i < NEC98_PARTITIONS; i++) {
		const struct nec98_partition *entry = &table[i];
		u16 start_cylinder, end_cylinder;

		if (!entry->mid && !entry->sid)
			continue;
		if (!entry->mid || !entry->sid ||
		    get_unaligned_le16(entry->reserved))
			return false;
		start_cylinder = get_unaligned_le16(&entry->start_cylinder);
		end_cylinder = get_unaligned_le16(&entry->end_cylinder);
		if (start_cylinder > end_cylinder ||
		    !nec98_chs_valid(entry->ipl_head, entry->ipl_sector,
				       heads, sectors) ||
		    !nec98_chs_valid(entry->start_head, entry->start_sector,
				       heads, sectors) ||
		    !nec98_chs_valid(entry->end_head, entry->end_sector,
				       heads, sectors))
			return false;
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
	int found = 0;
	int i;

	if (queue_logical_block_size(state->disk->queue) != 512)
		return 0;

	if (!pc9800_get_boot_disk_geometry(&heads, &sectors)) {
		/* Legacy loaders and direct kernel boots used the NEC IDE default. */
		heads = 8;
		sectors = 17;
		pr_warn_once("NEC98: no BIOS geometry supplied; using 8/17\n");
	}

	/* Genuine NEC IPLs and this project's free IPL both end in 55 AA. */
	boot = read_part_sector(state, 0, &boot_sector);
	if (!boot)
		return -1;
	if (get_unaligned_le16(boot + 510) != 0xaa55) {
		put_dev_sector(boot_sector);
		return 0;
	}
	put_dev_sector(boot_sector);

	table = read_part_sector(state, NEC98_PARTITION_SECTOR, &table_sector);
	if (!table)
		return -1;
	if (!nec98_table_valid((const struct nec98_partition *)table,
			       heads, sectors)) {
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
		found++;
	}

	put_dev_sector(table_sector);
	if (!found)
		return 0;

	seq_buf_puts(&state->pp_buf, " NEC98\n");
	return 1;
}
