OUTPUT_FORMAT("elf32-sh")
OUTPUT_ARCH(sh)
ENTRY(_start)

MEMORY
{
	/* 0x00000-0x01fff : boot d'origine conserve ; 0x1ff00-0x1ff3f : constantes lues par DMA */
	rom (rx) : ORIGIN = 0x00002000, LENGTH = 0x1df00
	rom2 (r) : ORIGIN = 0x00020000, LENGTH = 0x60000
	ram (rw) : ORIGIN = 0x02000000, LENGTH = 0x70000
}

SECTIONS
{
	.text : {
		*(.text.start)
		*(.text .text.*)
	} > rom
	.rodata : {
		*(.rodata .rodata.* .rdata)
	} > rom2
	.data : { *(.data .data.*) } > ram AT > rom2
	.bss (NOLOAD) : {
		__bss_start = .;
		*(.bss .bss.* COMMON)
		. = ALIGN(4);
		__bss_end = .;
	} > ram
	/DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
