OUTPUT_FORMAT(elf32-i386)
ENTRY(applet_entry)
SECTIONS {
	. = 0x50000;
	__applet_start = .;
	.header : { KEEP(*(.header)) }
	.text : { *(.text*) *(.rodata*) }
	.data : { *(.data*) }
	__applet_end = .;
}

