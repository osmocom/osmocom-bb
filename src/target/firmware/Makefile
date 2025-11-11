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
BOARDS?=compal_e88 compal_e86 compal_e99 se_j100 se_k2xx gta0x gtm900b fcdev3b \
	pirelli_dpl10 tr800

# Framebuffer support, board specific drivers
FB_OBJS=fb/framebuffer.o fb/font.o fb/helvR08.o fb/helvB14.o fb/c64.o \
	fb/symbols.o

FB_e88_OBJS=$(FB_OBJS) fb/fb_bw8.o fb/fb_st7558.o
FB_e99_OBJS=$(FB_OBJS) fb/fb_rgb332.o fb/fb_ssd1783.o
FB_e86_OBJS=$(FB_OBJS) fb/fb_rgb332.o fb/fb_td014.o
FB_j100_OBJS=$(FB_OBJS) fb/fb_rgb332.o fb/fb_ssd1963.o
FB_dpl10_OBJS=$(FB_OBJS) fb/fb_rgb332.o fb/fb_s6b33b1x.o
FB_k2xx_OBJS=$(FB_OBJS) fb/fb_rgb332.o fb/fb_k2xx.o
FB_dummy_OBJS=$(FB_OBJS) fb/fb_dummy.o

# TI Calypso
calypso_COMMON_OBJS=board/common/calypso_uart.o \
	board/common/calypso_pwl.o board/common/tx_calchan.o

# OpenMoko GTA0x
BOARD_gta0x_OBJS=$(calypso_COMMON_OBJS) board/gta0x/init.o \
	board/gta0x/rffe_gta0x_triband.o board/gta0x/rf_tables.o \
	board/gta0x/afcparams.o \
	board/common/readcal_tiffs.o battery/dummy.o $(FB_dummy_OBJS)
BOARD_gta0x_ENVIRONMENTS=highram

# Huawei GTM900-B
BOARD_gtm900b_OBJS=$(calypso_COMMON_OBJS) board/gtm900b/init.o \
	board/gtm900b/rffe_gtm900b.o board/gta0x/rf_tables.o \
	board/gtm900b/afcparams.o \
	board/common/readcal_tiffs.o battery/dummy.o $(FB_dummy_OBJS)
BOARD_gtm900b_ENVIRONMENTS=highram

# FreeCalypso FCDEV3B
BOARD_fcdev3b_OBJS=$(calypso_COMMON_OBJS) board/fcdev3b/init.o \
	board/gta0x/rffe_gta0x_triband.o board/gta0x/rf_tables.o \
	board/gta0x/afcparams.o \
	board/common/readcal_tiffs.o battery/dummy.o $(FB_dummy_OBJS)
BOARD_fcdev3b_ENVIRONMENTS=highram

# iWOW TR-800 aka FreeCalypso Tango
BOARD_tr800_OBJS=$(calypso_COMMON_OBJS) board/tr800/init.o \
	board/tr800/rffe_leo_quadband.o board/gta0x/rf_tables.o \
	board/tr800/afcparams.o \
	board/common/readcal_tiffs.o battery/dummy.o $(FB_dummy_OBJS)
BOARD_tr800_ENVIRONMENTS=highram

# Pirelli DP-L10
BOARD_pirelli_dpl10_OBJS=$(calypso_COMMON_OBJS) board/pirelli_dpl10/init.o \
	board/pirelli_dpl10/rffe_dpl10_triband.o \
	board/pirelli_dpl10/rf_tables.o board/pirelli_dpl10/readcal.o \
	battery/dummy.o $(FB_dpl10_OBJS)
BOARD_pirelli_dpl10_ENVIRONMENTS=highram

# Compal Generic
compal_COMMON_OBJS=$(calypso_COMMON_OBJS) \
	board/compal/rffe_dualband.o board/compal/rf_tables.o \
	board/compal/readcal_common.o
compal_COMMON_ENVIRONMENTS=compalram highram

# Compal E88
BOARD_compal_e88_OBJS=$(compal_COMMON_OBJS) board/compal_e88/init.o \
	board/compal/readcal_small.o board/compal_e88/tx_ramps.o \
	battery/compal_e88.o $(FB_e88_OBJS)
BOARD_compal_e88_ENVIRONMENTS=$(compal_COMMON_ENVIRONMENTS) e88loader e88flash

# Compal E86 (has a different RFFE configuration)
BOARD_compal_e86_OBJS=$(calypso_COMMON_OBJS) board/compal_e86/init.o \
	board/compal_e86/rffe_dualband_e86.o board/compal/rf_tables.o \
	board/compal/readcal_common.o board/compal/readcal_small.o \
	board/compal_e86/tx_ramps.o battery/dummy.o $(FB_e86_OBJS)
BOARD_compal_e86_ENVIRONMENTS=$(compal_COMMON_ENVIRONMENTS)

# Compal E99
BOARD_compal_e99_OBJS=$(compal_COMMON_OBJS) board/compal_e99/init.o \
	board/compal_e99/readcal.o board/compal_e88/tx_ramps.o \
	battery/dummy.o $(FB_e99_OBJS)
BOARD_compal_e99_ENVIRONMENTS=$(compal_COMMON_ENVIRONMENTS)

# Sony Ericsson J100 (made by Compal)
BOARD_se_j100_OBJS=$(compal_COMMON_OBJS) board/se_j100/init.o \
	board/compal/readcal_small.o board/se_j100/tx_ramps.o \
	battery/dummy.o $(FB_j100_OBJS)
BOARD_se_j100_ENVIRONMENTS=$(compal_COMMON_ENVIRONMENTS)

# Sony Ericsson K200i/K220i
BOARD_se_k2xx_OBJS=$(calypso_COMMON_OBJS) board/se_k2xx/init.o \
	board/se_k2xx/rffe_k2xx.o \
	board/gta0x/rf_tables.o board/gta0x/afcparams.o \
	board/common/readcal_tiffs.o battery/dummy.o $(FB_k2xx_OBJS)
BOARD_se_k2xx_ENVIRONMENTS=highram

#
# Applications
#

# List of all applications (meant to be overridden on command line)
APPLICATIONS?=hello_world compal_dsp_dump layer1 loader rssi menu

# Applications specific env requirements
APP_loader_ENVIRONMENTS=compalram highram
APP_rssi_ENVIRONMENTS=* -compalram
APP_menu_ENVIRONMENTS=* -highram

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
		tiffs/libtiffs.a \
		/tmp/arm/install/lib/libosmocore.a \
		/tmp/arm/install/lib/libosmogsm.a


#
# Build rules
#

# Global include path
INCLUDES=-Iinclude/ -I../../../include -I/tmp/arm/install/include

# Libraries are defined in subdirectories
-include calypso/Makefile
-include layer1/Makefile
-include comm/Makefile
-include tiffs/Makefile
-include lib/Makefile

# Include toolchain configuration
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
