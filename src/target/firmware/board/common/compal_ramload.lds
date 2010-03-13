OUTPUT_FORMAT("elf32-littlearm", "elf32-littlearm", "elf32-littlearm")
OUTPUT_ARCH(arm)
ENTRY(_start)
MEMORY
{
	/* compal-loaded binary: our text, initialized data */
	LRAM (rw) : ORIGIN = 0x00800000, LENGTH = 0x00010000
	/* compal-loaded binary: our unitialized data, stacks, heap */
	IRAM (rw) : ORIGIN = 0x00810000, LENGTH = 0x00010000
}
SECTIONS
{
	. = 0x800000;

	/* romloader data section, contains passthru interrupt vectors */
	.compal.loader (NOLOAD) : { . = 0x100; } > LRAM

	/* image signature (prepended by osmocon according to phone type) */
	.compal.header (NOLOAD) : { . = 4; } > LRAM

	/* initialization code */
	. = ALIGN(4);
	.text.start : {
		PROVIDE(_start = .);
		KEEP(*(.text.start))
		*(.text.start)
		_exceptions = .;
	} > LRAM

	/* exception vectors from 0x80001c to 0x800034 */
	.text.exceptions 0x80001c : AT (LOADADDR(.text.start) + SIZEOF(.text.start)) {
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
		/* gcc voodoo */
		*(.glue_7t) *(.glue_7) *(.vfp11_veneer) *(.v4_bx)
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
		*(.got.plt) *(.igot.plt) *(.got) *(.igot)
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
