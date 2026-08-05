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
}
