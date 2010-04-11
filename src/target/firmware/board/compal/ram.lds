/*
 * Linker script for running from internal SRAM on Compal phones
 *
 * This script is tailored specifically to the requirements imposed
 * on us by the Compal bootloader.
 *
 */
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
	} > LRAM

	/* exception vectors from 0x80001c to 0x800034 */
	.text.exceptions 0x80001c : AT (LOADADDR(.text.start) + SIZEOF(.text.start)) {
		KEEP(*(.text.exceptions))
		* (.text.exceptions)
		. = ALIGN(4);
	} > LRAM
	PROVIDE(_exceptions = LOADADDR(.text.exceptions));

	/* code */
	. = ALIGN(4);
	.text (LOADADDR(.text.exceptions) + SIZEOF(.text.exceptions)) :
	      AT (LOADADDR(.text.exceptions) + SIZEOF(.text.exceptions)) {
		/* regular code */
		*(.text*)
		/* gcc voodoo */
		*(.glue_7t) *(.glue_7) *(.vfp11_veneer) *(.v4_bx)
		. = ALIGN(4);

		/* constructors and destructors */
		. = ALIGN(4);
		__CTOR_LIST__ = .;
		LONG((__CTOR_END__ - __CTOR_LIST__) / 4 - 2)
		KEEP(*(SORT(.ctors)))
		LONG(0)				/* end of list */
		__CTOR_END__ = .;
		__DTOR_LIST__ = .;
		LONG((__DTOR_END__ - __DTOR_LIST__) / 4 - 2)
		KEEP(*(SORT(.dtors)))
		LONG(0)				/* end of list */
		__DTOR_END__ = .;
	} > LRAM
	PROVIDE(_text_start = LOADADDR(.text));
	PROVIDE(_text_end = LOADADDR(.text) + SIZEOF(.text));

	/* read-only data */
	. = ALIGN(4);
	.rodata : {
		*(.rodata*)
	} > LRAM
	PROVIDE(_rodata_start = LOADADDR(.rodata));
	PROVIDE(_rodata_end = LOADADDR(.rodata) + SIZEOF(.rodata));

	/* initialized data */
	. = ALIGN(4);
	.data : {
		*(.data)
	} > LRAM
	PROVIDE(_data_start = LOADADDR(.data));
	PROVIDE(_data_end = LOADADDR(.data) + SIZEOF(.data));

	/* pic offset tables */
	. = ALIGN(4);
	.got : {
		*(.got)
		*(.got.plt) *(.igot.plt) *(.got) *(.igot)
	} > LRAM
	PROVIDE(_got_start = LOADADDR(.got));
	PROVIDE(_got_end = LOADADDR(.got) + SIZEOF(.got));

	/* uninitialized data */
	.bss (NOLOAD) : {
		. = ALIGN(4);
		__bss_start = .;
		*(.bss)
	} > IRAM
	. = ALIGN(4);
	__bss_end = .;
	PROVIDE(_bss_start = __bss_start);
	PROVIDE(_bss_end = __bss_end);

	/* end of image */
	. = ALIGN(4);
	_end = .;
	PROVIDE(end = .);
}
