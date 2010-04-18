
# Global include path
INCLUDES=-Iinclude/ -I../../../include -I../../shared/libosmocore/include

# Various objects that are currently linked into all applications
FLASH_OBJS=flash/cfi_flash.o
DISPLAY_OBJS=display/font_r8x8.o display/font_r8x8_horiz.o display/st7558.o display/ssd1783.o display/display.o
ABB_OBJS=abb/twl3025.o
RF_OBJS=rf/trf6151.o

# List of all supported boards
BOARDS?=compal_e88 compal_e99 gta0x

# TI Calypso

calypso_COMMON_OBJS=board/common/calypso_uart.o board/common/calypso_pwl.o

# OpenMoko GTA0x

gta0x_COMMON_OBJS=board/common/rffe_gta0x_triband.o $(calypso_COMMON_OBJS)

# Compal Generic

compal_COMMON_OBJS=$(calypso_COMMON_OBJS) board/compal/rffe_dualband.o
compal_COMMON_ENVIRONMENTS=compalram

compalram_LDS=board/compal/ram.lds
compalram_OBJS=board/compal/start.ram.o board/compal/exceptions_redirected.o board/compal/handlers.o

# Compal E88

compal_e88_OBJS=$(compal_COMMON_OBJS) board/compal_e88/init.o
compal_e88_ENVIRONMENTS=$(compal_COMMON_ENVIRONMENTS) e88loader

e88loader_LDS=board/compal_e88/loader.lds
e88loader_OBJS=board/compal/start.rom.o board/compal/header.o board/compal/exceptions_redirect.o

e88flash_LDS=board/compal_e88/flash.lds board/compal/exceptions_full.o

# Compal E99

compal_e99_OBJS=$(compal_COMMON_OBJS) board/compal_e99/init.o

gta0x_OBJS=$(gta0x_COMMON_OBJS) board/gta0x/init.o

compal_e99_ENVIRONMENTS=$(compal_COMMON_ENVIRONMENTS)

e99loader_LDS=board/compal_e99/loader.lds
e99loader_OBJS=board/compal/header.o
e99flash_LDS=board/compal_e99/flash.lds


# List of all applications (add yours here!)
APPLICATIONS=hello_world compal_dsp_dump layer1 loader simtest

# Things that go in all applications
ANY_APP_OBJS+=$(ABB_OBJS) $(RF_OBJS) $(DISPLAY_OBJS) $(FLASH_OBJS)
ANY_APP_LIBS+=calypso/libcalypso.a layer1/liblayer1.a lib/libmini.a comm/libcomm.a ../../shared/libosmocore/build-target/src/.libs/libosmocore.a

# Libraries are defined in subdirectories
-include calypso/Makefile
-include layer1/Makefile
-include comm/Makefile
-include lib/Makefile

# Include rules
-include Makefile.inc
