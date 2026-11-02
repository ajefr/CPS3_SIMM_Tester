OUTPUT_FORMAT("elf32-sh")
OUTPUT_ARCH(sh)
ENTRY(_boot)

/* Image complete de la 29F400 (512 Ko), sans code d'origine :
   0x00000 vecteurs, 0x00400 demarrage, 0x00510 routine copiee dans le cache,
   0x00550 suite du demarrage, 0x02000 programme, 0x1FF00 constantes lues par
   DMA (en clair), 0x20000 donnees. */
MEMORY
{
	vec (rx)   : ORIGIN = 0x00000000, LENGTH = 0x400
	boot (rx)  : ORIGIN = 0x00000400, LENGTH = 0x110
	cache (rx) : ORIGIN = 0x00000510, LENGTH = 0x40
	boot2 (rx) : ORIGIN = 0x00000550, LENGTH = 0x1ab0
	rom (rx)   : ORIGIN = 0x00002000, LENGTH = 0x1df00
	consts (r) : ORIGIN = 0x0001ff00, LENGTH = 0x6c
	rom2 (r)   : ORIGIN = 0x00020000, LENGTH = 0x5ff00
	ram (rw)   : ORIGIN = 0x02000000, LENGTH = 0x70000
}

SECTIONS
{
	.vectors : { KEEP(*(.boot.vectors)) } > vec
	.boot : { *(.boot.code) } > boot
	.cache : { *(.boot.cache) } > cache
	.boot2 : { *(.boot.code2) } > boot2
	.text : {
		*(.text.start)
		*(.text .text.*)
	} > rom
	.consts : { KEEP(*(.boot.consts)) } > consts
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
