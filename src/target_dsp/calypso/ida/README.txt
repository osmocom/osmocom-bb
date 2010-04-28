Here's a few steps to get started quickly and get something readable:

 - Compile a patched for the IDA TMS320C54 module

   I made several enhancement to it to support the calypso better (the tms320c54
   module is part of the SDK and can be modded and recompiled) :

   - Add support for memory mappings so that the same memory zone can
     'appear' at several place in the address space (to handle data & code
		 overlay)
   - Fix the section handling when loading a file:
     . to set XPC properly,
     . to not override section name
     . to support more than 2 sections
   - Fix a bug in cross reference detection when dealing with section
     having selectors != 0
   - Add stub support for the type system. This allows loading of a .h
     header file with the NDB structure definition
   - Add definition for the IO ports so that they are symbolically
     displayed

   I can't publically distribute the IDA processor module modification
   because even just the patch contains some hex-rays code, so I'll handle
   this on a case by case basis. (just ask me privately and we'll work it out)

 - Dump the DSP ROM

   Using the compal_dsp_dump.bin, you must create a text dump of the DSP ROM,
   just piping the console output to a text file.

 - Generate COFF image

   The dump2coff.py script can convert the text dump into a usable COFF file
   containing all the correct sections and addresses.

 - Load this COFF image into IDA

   In the load dialog make sure :
    - Uncheck the 'Fill segment gaps (COFF)' checkbox
    - Select 'TMS320C54' in 'Change processor'
    - In 'Analysis Options/Processor specific analysis options' :
      - 'Choose device name': CALYPSO
      - 'Data segment address': 0x80000000
      - 'Add mapping' (do it several time)
        - From 0x00000060 -> 0x80000060  size 0x6FA0
        - From 0x00010060 -> 0x80000060  size 0x6FA0
        - From 0x00020060 -> 0x80000060  size 0x6FA0
        - From 0x00030060 -> 0x80000060  size 0x6FA0
        - From 0x8000E000 -> 0x0000E000  size 0x2000

 - Set 'stub' compiler options to allow the type system to load .h files

   In 'Options/Compiler':
     - Compiler: 'GNU C++'
     - Calling convention: 'Cdecl'
     - Memory model: 'Code Near, Data Near'
     - Pointer size: 'Near 16bit, Far 32bit'
     - Include directory: '/usr/include' (or a directory with your includes
       ... needs to exist)

 - Load the NDB types

   - Load the ndb.h file
   - In the local types view, import all structure / enum into the database
   - Then declare the following symbol and set them as struct type
     appropriately.

     0x80000800 api_w_page_0	db_mcu_to_dsp
     0x80000814 api_w_page_1	db_mcu_to_dsp
     0x80000828 api_r_page_0	db_dsp_to_mcu
     0x8000083c api_r_page_1	db_dsp_to_mcu
     0x800008d4 ndb           	ndb_mcu_dsp

