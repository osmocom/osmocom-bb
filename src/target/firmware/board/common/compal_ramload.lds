OUTPUT_FORMAT("elf32-littlearm", "elf32-littlearm", "elf32-littlearm")
OUTPUT_ARCH(arm)
ENTRY(_start)
MEMORY
{
	/* area that can be initialized by the loader (plus some reserved stuff) */
	LRAM (rw) : ORIGIN = 0x00800000, LENGTH = 0x00010000
	/* remainder of internal ram, can be used for bss and the like */
	IRAM (rw) : ORIGIN = 0x00810000, LENGTH = 0x00030000
	/* external ram on a C123 */
	ERAM (rw) : ORIGIN = 0x01000000, LENGTH = 0x00040000
}
SECTIONS
{
	. = 0x800000;

	/* reserved (what is in here?) */
	.compal.reserved1 (NOLOAD) : { . = 0x100; } > LRAM

	/* XXX: leftovers from exception vector trickery development? */
	/* .compal.reserved1 (NOLOAD) : { . = 0x1C; } > LRAM */
	/* .compal.reserved2 (NOLOAD) : { . = 0xC8; } > LRAM */

	/* image signature (prepended by compal_dnload according to phone type) */
	.compal.header (NOLOAD) : { . = 4; } > LRAM

	/* code */
	. = ALIGN(4);
	.text_start : {
		/* initialization code */
		PROVIDE(_start = .);
		KEEP(*(.init))
		*(.text._start)
		_exceptions = .;
	} > LRAM

	/* exception vectors from 0x80001c to 0x800034 */
	.text.exceptions 0x80001c: AT (LOADADDR(.text_start) + SIZEOF(.text_start)) {
		KEEP(*(.text.exceptions))
		* (.text.exceptions)
		. = ALIGN(4);
	} > LRAM

	/* code */
	. = ALIGN(4);
	.text (LOADADDR(.text.exceptions) + SIZEOF(.text.exceptions)) :
	      AT (LOADADDR(.text.exceptions) + SIZEOF(.text.exceptions)) {
		/* regular code */
		*(.text*)
	} > LRAM

	/* read-only data */
	. = ALIGN(4);
	.rodata : {
		*(.rodata*) 
	} > LRAM

	/* initialized data */
	. = ALIGN(4);
		.data : {
		*(.data)
	} > LRAM

	/* pic offset tables */
	. = ALIGN(4);
	.got : {
		*(.got) 
	} > LRAM

	/* uninitialized data */
	.bss (NOLOAD) : {
		. = ALIGN(4);
		__bss_start = .;
		*(.bss) 
	} > IRAM
	. = ALIGN(4);
	__bss_end = .;

	/* end of image */
	. = ALIGN(4);
	_end = .;
	PROVIDE(end = .);
}
