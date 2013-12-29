/*
 * Linker script for flashed loader on the Compal E88
 *
 * This script creates a binary that can replace a standard firmware
 * located at 0x2000. It works in conjunction with the compal ramloader.
 *
 * The interrupt vectors and start address are at known, fixed offsets.
 *
 */
OUTPUT_FORMAT("elf32-littlearm", "elf32-littlearm", "elf32-littlearm")
OUTPUT_ARCH(arm)
ENTRY(_start)
MEMORY
{
    /* 2 MBytes of external flash memory */
    FLASH (rx) : ORIGIN = 0x00000000, LENGTH = 0x200000
    /* 256 kBytes of internal zero-waitstate sram */
    IRAM  (rw) : ORIGIN = 0x00800000, LENGTH = 0x040000
    /* 256 kBytes of external slow sram */
    ERAM  (rw) : ORIGIN = 0x01000000, LENGTH = 0x040000
}
SECTIONS
{
    /* Provide symbols for the compal loader */
    .compal.loader 0x00000000 (NOLOAD) : {
        _compal_loader_start = .;
        . = 0x2000;
        _compal_loader_end = .;
    } > FLASH

    /* Compal-style image header */
    .compal.header 0x00002000 : {
        _compal_header_start = .;
        KEEP(*(.compal.header))
        *(.compal.header)
        . = 0xA0;
        _compal_header_end = .;
    } > FLASH

    /* Compal-style vector table */
    .compal.vectors 0x000020A0 : {
        PROVIDE(_exceptions = .);
        KEEP(*(.text.exceptions))
        *(.text.exceptions)
    } > FLASH

    /* Compal-style entry point */
    .text.start 0x000020F8 : {
        PROVIDE(_start = .);
        KEEP(*(.text.start))
        *(.text.start)
    } > FLASH

    /* code */
    .text : {
        /* regular code */
        *(.text*)
        /* gcc voodoo */
        *(.glue_7t) *(.glue_7) *(.vfp11_veneer) *(.v4_bx)
    } > FLASH
    PROVIDE(_text_start = ADDR(.text));
    PROVIDE(_text_end = ADDR(.text) + SIZEOF(.text));

    /* constructor pointers */
    .ctors : {
        /* ctor count */
        LONG(SIZEOF(.ctors) / 4 - 2)
        /* ctor pointers */
        KEEP(*(SORT(.ctors)))
        KEEP(*(SORT(.init_array)))
        /* end of list */
        LONG(0)
    } > FLASH
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
    } > FLASH
    PROVIDE(_dtor_start = LOADADDR(.dtors));
    PROVIDE(_dtor_end = LOADADDR(.dtors) + SIZEOF(.dtors));

    /* read-only data */
    .rodata : {
        *(.rodata*)
    } > FLASH
    PROVIDE(_rodata_start = ADDR(.rodata));
    PROVIDE(_rodata_end = ADDR(.rodata) + SIZEOF(.rodata));

    /* pic offset tables */
    .got : {
        . = ALIGN(4);
        *(.got)
        *(.got.plt) *(.igot.plt) *(.got) *(.igot)
        . = ALIGN(4);
    } > FLASH
    PROVIDE(_got_start = ADDR(.got));
    PROVIDE(_got_end = ADDR(.got) + SIZEOF(.got));

    /* reserved ram  */
    .compal.reservedram 0x800000 (NOLOAD) : {
        . = 0xff;
    } > IRAM

    /* initialized data */
    .data : AT (LOADADDR(.got) + SIZEOF(.got)) {
        . = ALIGN(4);
        *(.data)
        . = ALIGN(4);
    } > IRAM
    PROVIDE(__data_start = LOADADDR(.data));
    PROVIDE(__data_end = LOADADDR(.data) + SIZEOF(.data));
    PROVIDE(_data_start = ADDR(.data));
    PROVIDE(_data_end = ADDR(.data) + SIZEOF(.data));

    /* ram code */
    .ramtext : AT (LOADADDR(.data) + SIZEOF(.data)) {
        . = ALIGN(4);
        *(.ramtext)
        . = ALIGN(4);
    } > IRAM
    PROVIDE(__ramtext_start = LOADADDR(.ramtext));
    PROVIDE(__ramtext_end = LOADADDR(.ramtext) + SIZEOF(.ramtext));
    PROVIDE(_ramtext_start = ADDR(.ramtext));
    PROVIDE(_ramtext_end = ADDR(.ramtext) + SIZEOF(.ramtext));

    /* uninitialized data */
    .bss (NOLOAD) : {
        . = ALIGN(4);
        *(.bss)
        . = ALIGN(4);
    } > IRAM
    PROVIDE(__bss_start = ADDR(.bss));
    PROVIDE(__bss_end = ADDR(.bss) + SIZEOF(.bss));
    PROVIDE(_bss_start = __bss_start);
    PROVIDE(_bss_end = __bss_end);

    /* end of image */
    . = ALIGN(4);
    _end = .;
    PROVIDE(end = .);
}
