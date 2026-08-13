/* Versioned PC-98 PBR -> IO.SYS bootstrap handoff at physical 0000:0700. */
	.set HANDOFF,             0x0700
	.set HANDOFF_MAGIC,       0x4839384c /* "L98H" */
	.set HANDOFF_VERSION,     1
	.set HANDOFF_SIZE,        24
	.set HO_MAGIC,            0
	.set HO_VERSION,          4
	.set HO_SIZE,             6
	.set HO_PARTITION_LBA,    8
	.set HO_BOOT_BIOS_ID,    12
	.set HO_HEADS,           13
	.set HO_SECTORS,         14
	.set HO_FILESYSTEM_HINT, 15
	.set HO_SECTOR_SIZE,     16
	.set HO_FLAGS,           18
	.set HO_SAVED_SI,        20
	.set HO_SAVED_DI,        22
	.set HO_FS_FAT16,         1
	.set HO_FLAG_GEOMETRY,    1

