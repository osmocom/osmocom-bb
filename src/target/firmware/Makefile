
# List of all supported boards (meant to be overridden on command line)
BOARDS?=compal_e88 compal_e99 gta0x

# List of all applications (meant to be overridden on command line)
APPLICATIONS?=hello_world compal_dsp_dump layer1 loader simtest chainload


# TI Calypso

calypso_COMMON_OBJS=board/common/calypso_uart.o board/common/calypso_pwl.o

# OpenMoko GTA0x

gta0x_COMMON_OBJS=$(calypso_COMMON_OBJS) board/common/rffe_gta0x_triband.o
gta0x_COMMON_ENVIRONMENTS=highram
gta0x_OBJS=$(gta0x_COMMON_OBJS) board/gta0x/init.o
gta0x_ENVIRONMENTS=$(gta0x_COMMON_ENVIRONMENTS)

# Compal Generic

compal_COMMON_OBJS=$(calypso_COMMON_OBJS) board/compal/rffe_dualband.o
compal_COMMON_ENVIRONMENTS=compalram highram

compalram_LDS=board/compal/ram.lds
compalram_OBJS=board/compal/start.ram.o board/compal/exceptions_redirected.o board/compal/handlers.o

highram_LDS=board/compal/highram.lds
highram_OBJS=board/compal/start.ram.o board/compal/exceptions_redirected.o board/compal/handlers.o

# Compal E88

compal_e88_OBJS=$(compal_COMMON_OBJS) board/compal_e88/init.o
compal_e88_ENVIRONMENTS=$(compal_COMMON_ENVIRONMENTS) e88loader e88flash

e88loader_LDS=board/compal_e88/loader.lds
e88loader_OBJS=board/compal/start.rom.o board/compal/header.o board/compal/exceptions_redirect.o

e88flash_LDS=board/compal_e88/flash.lds
e88flash_OBJS=board/compal/start.rom.o board/compal/header.o board/compal/exceptions_redirected.o board/compal/handlers.o

# Compal E99

compal_e99_OBJS=$(compal_COMMON_OBJS) board/compal_e99/init.o
compal_e99_ENVIRONMENTS=$(compal_COMMON_ENVIRONMENTS)

e99loader_LDS=board/compal_e99/loader.lds
e99loader_OBJS=board/compal/header.o
e99flash_LDS=board/compal_e99/flash.lds

# Global include path
INCLUDES=-Iinclude/ -I../../../include -I../dietlibc/include -I../../shared/libosmocore/include

# Various objects that are currently linked into all applications
FLASH_OBJS=flash/cfi_flash.o
DISPLAY_OBJS=display/font_r8x8.o display/font_r8x8_horiz.o display/st7558.o display/td014.o display/ssd1783.o display/display.o
ABB_OBJS=abb/twl3025.o
RF_OBJS=rf/trf6151.o

# Objects that go in all applications
ANY_APP_OBJS+=$(ABB_OBJS) $(RF_OBJS) $(DISPLAY_OBJS) $(FLASH_OBJS)
ANY_APP_LIBS+=calypso/libcalypso.a layer1/liblayer1.a lib/libmini.a comm/libcomm.a sys/libsys.a ../dietlibc/bin-arm/dietlibc.a ../../shared/libosmocore/build-target/src/.libs/libosmocore.a


# Libraries are defined in subdirectories
-include calypso/Makefile
-include layer1/Makefile
-include comm/Makefile
-include lib/Makefile
-include sys/Makefile

# Include rules
-include Makefile.inc

# Uncomment this line if you want to enable Tx (Transmit) Support.
#CFLAGS += -DCONFIG_TX_ENABLE

# Uncomment this line if you want to write to flash.
#CFLAGS += -DCONFIG_FLASH_WRITE

# Uncomment this line if you want to write to flash, including the bootloader.
#CFLAGS += -DCONFIG_FLASH_WRITE_LOADER
