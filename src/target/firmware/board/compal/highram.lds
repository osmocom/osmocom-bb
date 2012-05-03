/*
 * Linker script for running from upper internal RAM on the TI Calypso
 *
 * This script creates a binary that can be loaded into high ram on
 * all Calypso devices. It can be jumped into directly at the load
 * address.
 *
 * This is used for debugging the loader and for general hacking purposes.
 *
 */
OUTPUT_FORMAT("elf32-littlearm", "elf32-littlearm", "elf32-littlearm")
OUTPUT_ARCH(arm)
ENTRY(_start)
MEMORY
{
    /* lowram: could be anything, we place exception vectors here */
	XRAM (rw) : ORIGIN = 0x00800000, LENGTH = 0x00020000
    /* highram binary: our text, initialized data */
    LRAM (rw) : ORIGIN = 0x00820000, LENGTH = 0x00014000
    /* highram binary: our unitialized data, stacks, heap */
    IRAM (rw) : ORIGIN = 0x00834000, LENGTH = 0x0000c000
}
SECTIONS
{
    . = 0x820000;

    /* initialization code */
    .text.start : {
        PROVIDE(_start = .);
        KEEP(*(.text.start))
        *(.text.start)
    } > LRAM

    /* exception vectors linked for 0x80001c to 0x800034 */
    .text.exceptions 0x80001c : AT (LOADADDR(.text.start) + SIZEOF(.text.start)) {
        KEEP(*(.text.exceptions))
        * (.text.exceptions)
        . = ALIGN(4);
    } > XRAM
    PROVIDE(_exceptions = LOADADDR(.text.exceptions));

    /* code */
    . = ALIGN(4);
    .text (LOADADDR(.text.exceptions) + SIZEOF(.text.exceptions)) :
          AT (LOADADDR(.text.exceptions) + SIZEOF(.text.exceptions)) {
        /* regular code */
        *(.text*)
        /* always-in-ram code */
        *(.ramtext*)
        /* gcc voodoo */
        *(.glue_7t) *(.glue_7) *(.vfp11_veneer) *(.v4_bx)
        . = ALIGN(4);
    } > LRAM
    PROVIDE(_text_start = LOADADDR(.text));
    PROVIDE(_text_end = LOADADDR(.text) + SIZEOF(.text));

    /* constructor pointers */
    .ctors : {
        /* ctor count */
        LONG(SIZEOF(.ctors) / 4 - 2)
        /* ctor pointers */
        KEEP(*(SORT(.ctors)))
        /* end of list */
        LONG(0)
    } > LRAM
    PROVIDE(_ctor_start = LOADADDR(.ctors));
    PROVIDE(_ctor_end = LOADADDR(.ctors) + SIZEOF(.ctors));

    /* destructor pointers */
    .dtors : {
        /* dtor count */
        LONG(SIZEOF(.dtors) / 4 - 2)
        /* dtor pointers */
        KEEP(*(SORT(.dtors)))
        /* end of list */
        LONG(0)
    } > LRAM
    PROVIDE(_dtor_start = LOADADDR(.dtors));
    PROVIDE(_dtor_end = LOADADDR(.dtors) + SIZEOF(.dtors));

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
