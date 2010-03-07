OUTPUT_FORMAT("elf32-littlearm", "elf32-littlearm", "elf32-littlearm")
OUTPUT_ARCH(arm)
ENTRY(_start)
MEMORY
{
	/* area that can be initialized by the loader (plus some reserved stuff) */
	LRAM (rw) : ORIGIN = 0x00800000, LENGTH = 0x00010000
	/* data/bss for osmoload */
	DRAM (rw) : ORIGIN = 0x00810000, LENGTH = 0x00010000
	/* remainder of internal ram, can be used for bss and the like */
	IRAM (rw) : ORIGIN = 0x00820000, LENGTH = 0x00020000
	/* external ram on a C123 */
	ERAM (rw) : ORIGIN = 0x01000000, LENGTH = 0x00040000
}
SECTIONS
{
	. = 0x820000;

	/* code */
	. = ALIGN(4);
	.text_start : {
		/* initialization code */
		PROVIDE(_start = .);
		KEEP(*(.init))
		*(.text._start)
		_exceptions = .;
	} > IRAM

	/* exception vectors from 0x80001c to 0x800034 */
	.text.exceptions 0x82001c: AT (LOADADDR(.text_start) + SIZEOF(.text_start)) {
		KEEP(*(.text.exceptions))
		* (.text.exceptions)
		. = ALIGN(4);
	} > IRAM

	/* code */
	. = ALIGN(4);
	.text (LOADADDR(.text.exceptions) + SIZEOF(.text.exceptions)) :
	      AT (LOADADDR(.text.exceptions) + SIZEOF(.text.exceptions)) {
		/* regular code */
		*(.text*)
	} > IRAM

	/* read-only data */
	. = ALIGN(4);
	.rodata : {
		*(.rodata) 
	} > IRAM

	/* initialized data */
	. = ALIGN(4);
		.data : {
		*(.data)
	} > IRAM

	/* pic offset tables */
	. = ALIGN(4);
	.got : {
		*(.got) 
	} > IRAM

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
