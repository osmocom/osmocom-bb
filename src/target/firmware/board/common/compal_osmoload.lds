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
	} > IRAM

	/* exception vectors from 0x80001c to 0x800034 */
	.text.exceptions 0x80001c : AT (LOADADDR(.text.start) + SIZEOF(.text.start)) {
		KEEP(*(.text.exceptions))
		* (.text.exceptions)
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
	PROVIDE(_exceptions = LOADADDR(.text.exceptions));

	/* code */
	. = ALIGN(4);
	.text (LOADADDR(.text.exceptions) + SIZEOF(.text.exceptions)) :
	      AT (LOADADDR(.text.exceptions) + SIZEOF(.text.exceptions)) {
		/* regular code */
		*(.text*)
		/* gcc voodoo */
		*(.glue_7t) *(.glue_7) *(.vfp11_veneer) *(.v4_bx)
	} > IRAM
	PROVIDE(_text_start = LOADADDR(.text));
	PROVIDE(_text_end = LOADADDR(.text) + SIZEOF(.text));

	/* read-only data */
	. = ALIGN(4);
	.rodata : {
		*(.rodata*)
	} > IRAM
	PROVIDE(_rodata_start = LOADADDR(.rodata));
	PROVIDE(_rodata_end = LOADADDR(.rodata) + SIZEOF(.rodata));

	/* initialized data */
	. = ALIGN(4);
	.data : {
		*(.data)
	} > IRAM
	PROVIDE(_data_start = LOADADDR(.data));
	PROVIDE(_data_end = LOADADDR(.data) + SIZEOF(.data));

	/* pic offset tables */
	. = ALIGN(4);
	.got : {
		*(.got)
		*(.got.plt) *(.igot.plt) *(.got) *(.igot)
	} > IRAM
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
