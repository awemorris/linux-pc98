/*
 * PC-9800 Bootloader
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef BOOT98_ABI_H
#define BOOT98_ABI_H

/* Shared data contract between the real-mode Stage 1 and 32-bit Stage 2. */
#include <stdint.h>

#define BOOT98_STAGE2_MAGIC 0x53383942U  /* "B98S" */
#define BOOT98_HANDOFF_MAGIC 0x48323842U /* "B82H" */
#define BOOT98_BOOTSTRAP_MAGIC 0x4839384cU /* "L98H" */

enum boot98_bootstrap_filesystem {
	BOOT98_BOOTSTRAP_FS_FAT16 = 1,
};

enum boot98_bootstrap_flags {
	BOOT98_BOOTSTRAP_HAS_GEOMETRY = 1U << 0,
};

/* Versioned PBR -> IO.SYS contract at physical address 0000:0700. */
struct boot98_bootstrap_handoff {
	uint32_t magic;
	uint16_t version;
	uint16_t size;
	uint32_t partition_lba;
	uint8_t boot_bios_id;
	uint8_t heads;
	uint8_t sectors;
	uint8_t filesystem_hint;
	uint16_t sector_size;
	uint16_t flags;
	uint16_t saved_si;
	uint16_t saved_di;
} __attribute__((packed));

struct boot98_stage2_header {
	uint32_t magic;
	uint16_t version;
	uint16_t header_size;
	uint32_t image_size;
	uint32_t entry_offset;
	uint32_t payload_checksum;
} __attribute__((packed));

struct boot98_handoff {
	uint32_t magic;
	uint16_t version;
	uint16_t size;
	uint8_t device_count;
	uint8_t boot_bios_id;
	uint16_t reserved;
	uint32_t device_table;
	uint32_t bios_gateway;
} __attribute__((packed));

enum boot98_bios_service {
	BOOT98_BIOS_DISK_READ = 1,
	BOOT98_BIOS_KEY_READ = 2,
	BOOT98_BIOS_KEY_POLL = 3,
	BOOT98_BIOS_DISPLAY_RESET = 4,
	BOOT98_BIOS_RETURN_MENU = 5,
	BOOT98_BIOS_IPLWARE = 6,
	BOOT98_BIOS_REPROBE = 7,
	BOOT98_BIOS_CHAIN_BOOT = 8,
	BOOT98_BIOS_CLOCK_SECOND = 9,
	/* Probe exactly request.bios_id; request.status is a device-class hint. */
	BOOT98_BIOS_PROBE_FIXED = 10,
};

struct boot98_bios_request {
	uint16_t service;
	uint16_t status;
	uint8_t bios_id;
	uint8_t heads;
	uint8_t sectors;
	uint8_t reserved;
	uint32_t lba;
	uint32_t buffer;
} __attribute__((packed));

#define BOOT98_APPLET_MAGIC 0x41383942U /* "B98A" */
struct boot98_applet_header {
	uint32_t magic;
	uint16_t abi_version;
	uint16_t header_size;
	uint32_t image_size;
	uint16_t entry_offset;
	uint16_t flags;
	uint32_t crc32;
	char name[16];
} __attribute__((packed));

struct boot98_applet_services {
	uint16_t abi_version;
	uint16_t size;
	void (*putc)(char c);
	void (*puts)(const char *s);
	uint32_t (*key_read)(void);
};

typedef uint32_t (*boot98_applet_entry_t)(
        const struct boot98_applet_services *services, uint32_t argc,
        const char *const *argv);

typedef uint32_t (*boot98_bios_gateway_t)(struct boot98_bios_request *request);

_Static_assert(sizeof(struct boot98_stage2_header) == 20,
               "BOOT98 Stage 2 header must remain 20 bytes");
_Static_assert(sizeof(struct boot98_bootstrap_handoff) == 24,
               "BOOT98 bootstrap handoff must remain 24 bytes");
_Static_assert(sizeof(struct boot98_handoff) == 20,
               "BOOT98 handoff must remain 20 bytes");
_Static_assert(sizeof(struct boot98_bios_request) == 16,
               "BOOT98 BIOS request must remain 16 bytes");
_Static_assert(sizeof(struct boot98_applet_header) == 36,
               "BOOT98 applet header must remain 36 bytes");

enum boot98_device_class {
	BOOT98_DEV_FDD = 1,
	BOOT98_DEV_IDE = 2,
	BOOT98_DEV_SCSI = 3,
};

enum boot98_device_flags {
	BOOT98_DEV_PRESENT = 1U << 0,
	BOOT98_DEV_HAS_GEOMETRY = 1U << 1,
	BOOT98_DEV_BOOT_ORIGIN = 1U << 2,
};

/* flags in the Linux SETUP_PC98_DISK payload */
enum boot98_linux_disk_flags {
	BOOT98_LINUX_DISK_F_FALLBACK = 1U << 0,
	BOOT98_LINUX_DISK_F_BOOT = 1U << 1,
};

struct boot98_device {
	uint8_t device_class;
	uint8_t display_index;
	uint8_t bios_id;
	uint8_t flags;
	uint16_t sector_size;
	uint16_t cylinders;
	uint8_t heads;
	uint8_t sectors;
	uint8_t controller_location;
	uint8_t reserved[5];
} __attribute__((packed));

_Static_assert(sizeof(struct boot98_device) == 16,
               "BOOT98 device descriptor ABI must remain 16 bytes");

#endif
