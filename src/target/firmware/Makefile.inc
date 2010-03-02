CROSS_COMPILE?=arm-elf-
CC=gcc
LD=ld
SIZE=size
OBJCOPY=objcopy

DEBUGF=dwarf-2

CFLAGS=-mcpu=arm7tdmi $(INCLUDES)
CFLAGS += -Wall -Wextra -Wcast-align -Wimplicit -Wunused
CFLAGS += -Wswitch -Wredundant-decls -Wreturn-type -Wshadow -Wnested-externs
CFLAGS += -Wbad-function-cast -Wsign-compare -Waggregate-return
CFLAGS += -Wa,-adhlns=$(subst $(suffix $<),.lst,$<)
CFLAGS += -Os -ffunction-sections
CFLAGS += -g$(DEBUGF)

# Uncomment this line if you want to enable Tx (Transmit) Support.
#CFLAGS += -DCONFIG_TX_ENABLE

# some older toolchains don't support this, ignore it for now
#ASFLAGS=-Wa,-adhlns=$(<:.S=.lst),--g$(DEBUGF) $(INCLUDES) -D__ASSEMBLY__
ASFLAGS=-Wa,-adhlns=$(<:.S=.lst) $(INCLUDES) -D__ASSEMBLY__

LDFLAGS = -nostartfiles -nostdlib -nodefaultlibs --gc-sections #-Wl,-Map=$(TARGET).map,--cref
