OUTPUT_FORMAT("elf32-littlearm", "elf32-littlearm", "elf32-littlearm")
OUTPUT_ARCH(arm)
ENTRY(_start)
MEMORY
{
	/* compal-loaded binary: text, initialized data */
	LRAM (rw) : ORIGIN = 0x00800000, LENGTH = 0x00010000
	/* compal-loaded binary: unitialized data, stacks, heap */
	DRAM (rw) : ORIGIN = 0x00810000, LENGTH = 0x00010000
	/* remainder of internal ram: this is where we are linking to */
	IRAM (rw) : ORIGIN = 0x00820000, LENGTH = 0x00020000
}
SECTIONS
{
	. = 0x820000;

	/* initialization code */
	. = ALIGN(4);
	.text.start : {
		PROVIDE(_start = .);
		KEEP(*(.text.start))
		*(.text.start)
		_exceptions = .;
	} > IRAM

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
	} > IRAM

	/* read-only data */
	. = ALIGN(4);
	.rodata : {
		*(.rodata*)
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
		*(.got.plt) *(.igot.plt) *(.got) *(.igot)
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
