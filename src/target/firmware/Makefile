##
## Osmocom-BB: Target firmware Makefile
##

#
# Environments
#

ENV_compalram_LDS=board/compal/ram.lds
ENV_compalram_OBJS=board/compal/start.ram.o board/compal/exceptions_redirected.o board/compal/handlers.o

ENV_highram_LDS=board/compal/highram.lds
ENV_highram_OBJS=board/compal/start.ram.o board/compal/exceptions_redirected.o board/compal/handlers.o

ENV_e88loader_LDS=board/compal_e88/loader.lds
ENV_e88loader_OBJS=board/compal/start.rom.o board/compal/header.o board/compal/exceptions_redirect.o

ENV_e88flash_LDS=board/compal_e88/flash.lds
ENV_e88flash_OBJS=board/compal/start.rom.o board/compal/header.o board/compal/exceptions_redirected.o board/compal/handlers.o


#
# Boards
#

# List of all supported boards (meant to be overridden on command line)
BOARDS?=compal_e88 compal_e86 compal_e99 se_j100 gta0x pirelli_dpl10

# Framebuffer support, board specific drivers
FB_OBJS=fb/framebuffer.o fb/font.o fb/helvR08.o fb/helvB14.o fb/c64.o \
	fb/symbols.o

FB_e88_OBJS=$(FB_OBJS) fb/fb_bw8.o fb/fb_st7558.o
FB_e99_OBJS=$(FB_OBJS) fb/fb_rgb332.o fb/fb_ssd1783.o
FB_e86_OBJS=$(FB_OBJS) fb/fb_rgb332.o fb/fb_td014.o
FB_j100_OBJS=$(FB_OBJS) fb/fb_rgb332.o fb/fb_ssd1963.o
FB_dpl10_OBJS=$(FB_OBJS) fb/fb_rgb332.o fb/fb_s6b33b1x.o
FB_dummy_OBJS=$(FB_OBJS) fb/fb_dummy.o

# TI Calypso
calypso_COMMON_OBJS=board/common/calypso_uart.o board/common/calypso_pwl.o

# OpenMoko GTA0x
BOARD_gta0x_OBJS=$(calypso_COMMON_OBJS) board/gta0x/init.o \
	board/gta0x/rffe_gta0x_triband.o board/gta0x/rf_power.o \
	battery/dummy.o $(FB_dummy_OBJS)
BOARD_gta0x_ENVIRONMENTS=highram

# Pirelli DP-L10
BOARD_pirelli_dpl10_OBJS=$(calypso_COMMON_OBJS) board/pirelli_dpl10/init.o \
	board/pirelli_dpl10/rffe_dpl10_triband.o board/pirelli_dpl10/rf_power.o \
	battery/dummy.o $(FB_dpl10_OBJS)
BOARD_pirelli_dpl10_ENVIRONMENTS=highram

# Compal Generic
compal_COMMON_OBJS=$(calypso_COMMON_OBJS) \
	board/compal/rffe_dualband.o board/compal/rf_power.o
compal_COMMON_ENVIRONMENTS=compalram highram

# Compal E88
BOARD_compal_e88_OBJS=$(compal_COMMON_OBJS) board/compal_e88/init.o \
	battery/compal_e88.o $(FB_e88_OBJS)
BOARD_compal_e88_ENVIRONMENTS=$(compal_COMMON_ENVIRONMENTS) e88loader e88flash

# Compal E86 (has a different RFFE configuration)
BOARD_compal_e86_OBJS=$(calypso_COMMON_OBJS) board/compal_e86/init.o \
	board/compal_e86/rffe_dualband_e86.o board/compal/rf_power.o \
	battery/dummy.o $(FB_e86_OBJS)
BOARD_compal_e86_ENVIRONMENTS=$(compal_COMMON_ENVIRONMENTS)

# Compal E99
BOARD_compal_e99_OBJS=$(compal_COMMON_OBJS) board/compal_e99/init.o \
	battery/dummy.o $(FB_e99_OBJS)
BOARD_compal_e99_ENVIRONMENTS=$(compal_COMMON_ENVIRONMENTS)

# Sony Ericsson J100 (made by Compal)
BOARD_se_j100_OBJS=$(compal_COMMON_OBJS) board/se_j100/init.o \
	battery/dummy.o $(FB_j100_OBJS)
BOARD_se_j100_ENVIRONMENTS=$(compal_COMMON_ENVIRONMENTS)


#
# Applications
#

# List of all applications (meant to be overridden on command line)
APPLICATIONS?=hello_world compal_dsp_dump layer1 loader rssi

# Applications specific env requirements
APP_loader_ENVIRONMENTS=compalram highram
APP_rssi_ENVIRONMENTS=* -compalram

# Various objects that are currently linked into all applications
FLASH_OBJS=flash/cfi_flash.o
ABB_OBJS=abb/twl3025.o
RF_OBJS=rf/trf6151.o

# Objects that go in all applications
ANY_APP_OBJS+=$(ABB_OBJS) $(RF_OBJS) $(FLASH_OBJS)
ANY_APP_LIBS+=	calypso/libcalypso.a \
		layer1/liblayer1.a \
		lib/libmini.a \
		comm/libcomm.a \
		../../shared/libosmocore/build-target/src/.libs/libosmocore.a \
		../../shared/libosmocore/build-target/src/gsm/.libs/libosmogsm.a


#
# Build rules
#

# Global include path
INCLUDES=-Iinclude/ -I../../../include -I../../shared/libosmocore/include

# Libraries are defined in subdirectories
-include calypso/Makefile
-include layer1/Makefile
-include comm/Makefile
-include lib/Makefile

# Include rules
-include Makefile.inc


#
# Build options
#

# Uncomment this line if you want to enable Tx (Transmit) Support.
#CFLAGS += -DCONFIG_TX_ENABLE

# Uncomment this line if you want to write to flash.
#CFLAGS += -DCONFIG_FLASH_WRITE

# Uncomment this line if you want to write to flash, including the bootloader.
#CFLAGS += -DCONFIG_FLASH_WRITE_LOADER
