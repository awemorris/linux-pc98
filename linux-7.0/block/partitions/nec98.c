// SPDX-License-Identifier: GPL-2.0-only
/*
 * NEC PC-9800 fixed-disk partition table support
 *
 * The table occupies LBA 1 and contains sixteen packed 32-byte entries.
 * CHS fields are zero based.  qemu-pc98's IDE BIOS presents the traditional
 * eight-head, seventeen-sector geometry used here.
 */

#include <linux/unaligned.h>

#include "check.h"

#define NEC98_PARTITION_SECTOR	1
#define NEC98_PARTITIONS	16
#define NEC98_ENTRY_SIZE	32
#define NEC98_HEADS		8
#define NEC98_SECTORS		17

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

static bool nec98_chs_valid(u8 head, u8 sector)
{
	return head < NEC98_HEADS && sector < NEC98_SECTORS;
}

static sector_t nec98_chs_to_lba(u16 cylinder, u8 head, u8 sector)
{
	return ((sector_t)cylinder * NEC98_HEADS + head) * NEC98_SECTORS +
	       sector;
}

int nec98_partition(struct parsed_partitions *state)
{
	const struct nec98_partition *entry;
	unsigned char *boot, *table;
	sector_t capacity = get_capacity(state->disk);
	Sector boot_sector, table_sector;
	int found = 0;
	int i;

	if (queue_logical_block_size(state->disk->queue) != 512)
		return 0;

	/*
	 * "IPL1" is the free qemu-pc98 initialised-disk marker.  Requiring it
	 * prevents an arbitrary second sector from being mistaken for NEC98.
	 * Support for vendor IPL signatures can be added after real-disk tests.
	 */
	boot = read_part_sector(state, 0, &boot_sector);
	if (!boot)
		return -1;
	if (memcmp(boot + 4, "IPL1", 4)) {
		put_dev_sector(boot_sector);
		return 0;
	}
	put_dev_sector(boot_sector);

	table = read_part_sector(state, NEC98_PARTITION_SECTOR, &table_sector);
	if (!table)
		return -1;

	entry = (const struct nec98_partition *)table;
	for (i = 0; i < NEC98_PARTITIONS && i + 1 < state->limit;
	     i++, entry++) {
		sector_t start, end;
		u16 start_cylinder, end_cylinder;

		if (!entry->mid && !entry->sid)
			continue;

		start_cylinder = get_unaligned_le16(&entry->start_cylinder);
		end_cylinder = get_unaligned_le16(&entry->end_cylinder);
		if (!nec98_chs_valid(entry->start_head, entry->start_sector) ||
		    !nec98_chs_valid(entry->end_head, entry->end_sector))
			continue;

		start = nec98_chs_to_lba(start_cylinder, entry->start_head,
					 entry->start_sector);
		end = nec98_chs_to_lba(end_cylinder, entry->end_head,
				       entry->end_sector);
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

	strlcat(state->pp_buf, " NEC98\n", PAGE_SIZE);
	return 1;
}
