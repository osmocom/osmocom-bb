
# Global include path
INCLUDES=-Iinclude/ -I../../../include

# Various objects that are currently linked into all applications
FLASH_OBJS=flash/cfi_flash.o
DISPLAY_OBJS=display/font_r8x8.o display/font_r8x8_horiz.o display/st7558.o display/ssd1783.o
ABB_OBJS=abb/twl3025.o
RF_OBJS=rf/trf6151.o

# Board- and environment-specific startup code and linker script
START=board/common/compal_ramload_start.S
LDS=board/common/compal_ramload.lds

BOARDS=compal_e88 compal_e99
compal_e88_OBJS=board/common/rffe_compal_dualband.o board/compal_e88/init.o
compal_e99_OBJS=board/common/rffe_compal_dualband.o board/compal_e99/init.o

APPLICATIONS=hello_world l1test compal_dump compal_dsp_dump layer1 loader

# Things that go in all applications
ANY_APP_OBJS+=$(START:.S=.o) $(ABB_OBJS) $(RF_OBJS) $(DISPLAY_OBJS) $(FLASH_OBJS)
ANY_APP_LIBS+=calypso/libcalypso.a layer1/liblayer1.a lib/libmini.a comm/libcomm.a

# Libraries are defined in subdirectories
-include calypso/Makefile
-include layer1/Makefile
-include comm/Makefile
-include lib/Makefile

# Include rules
-include Makefile.inc
