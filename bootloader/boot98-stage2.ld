OUTPUT_FORMAT(elf32-i386)
ENTRY(_start32)
SECTIONS
{
	. = 0x00020000;
	.header : { KEEP(*(.header)) }
	.text : { *(.text.entry) *(.text*) }
	.rodata : { *(.rodata*) }
	.data : { *(.data*) }
	__image_end = .;
	__bss_start = .;
	.bss : { *(.bss*) *(COMMON) }
	__bss_end = .;
	ASSERT(__image_end - 0x00020000 <= 0x00050000,
	       "BOOT.SYS load image exceeds 320 KiB")
	ASSERT(__bss_end <= 0x00080000,
	       "BOOT.SYS image and BSS overlap boot parameters")
}
