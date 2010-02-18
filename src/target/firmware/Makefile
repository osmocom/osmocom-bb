INCLUDES=-Iinclude/ -I../../../include
-include Makefile.inc

# individual list of object files, they should probably become libraries
FLASH_OBJS=flash/cfi_flash.o
DISPLAY_OBJS=display/font_r8x8.o display/st7558.o
ABB_OBJS=abb/twl3025.o
RF_OBJS=rf/trf6151.o
BOARD_C123_OBJS=board/common/rffe_compal_dualband.o board/compal_e88/init.o
START=board/common/compal_ramload_start.S
LDS=board/common/compal_ramload.lds

# The objects that we want to link with every application
OBJS=start.o $(ABB_OBJS) $(RF_OBJS) $(DISPLAY_OBJS) $(FLASH_OBJS) $(BOARD_C123_OBJS)

# The libraries that we want to link with every application
LIBS=calypso/libcalypso.a layer1/liblayer1.a lib/libmini.a comm/libcomm.a

# The list of applications we ant to build.  Please add your apps here!
APPS=hello_world l1test compal_dump compal_dsp_dump layer1

APP_BINS=$(APPS:=.bin)
APP_ELFS=$(APPS:=.elf)
APP_OBJS=$(patsubst %,apps/%/main.o, $(APPS))
APP_SIZES=$(APP_ELFS:.elf=.size)

LST=$(OBJS:.o=.lst) $(APP_OBJS:.o=.lst) $(START:.S=.lst)

all:	$(APP_BINS) $(APP_ELFS) $(APP_SIZES)

start.o: $(START)
	$(CROSS_COMPILE)$(CC) $(CFLAGS) -c -o $@ $^

%.o: %.c
	$(CROSS_COMPILE)$(CC) $(CFLAGS) -c -o $@ $^

%.elf: $(OBJS) apps/%/main.o $(LIBS)
	$(CROSS_COMPILE)$(LD) $(LDFLAGS) -T $(LDS) -Bstatic -Map $@.map  -o $@ --start-group $^ --end-group
	$(CROSS_COMPILE)$(SIZE) $@

%.size: %.elf
	$(CROSS_COMPILE)$(SIZE) -A $^ > $@

%.bin: %.elf
	$(CROSS_COMPILE)objcopy --gap-fill=0xff -O binary $^ $@

# FIXME: we don't do dependencies into the subdirectories, so we always rebuild
.PHONY: calypso/libcalypso.a
calypso/libcalypso.a:
	make -C calypso all

# FIXME: we don't do dependencies into the subdirectories, so we always rebuild
.PHONY: layer1/liblayer1.a
layer1/liblayer1.a:
	make -C layer1 all

lib/libmini.a:
	make -C lib all

# FIXME: we don't do dependencies into the subdirectories, so we always rebuild
.PHONY: comm/libcomm.a
comm/libcomm.a:
	make -C comm all
clean:
	make -C calypso clean
	make -C layer1 clean
	make -C lib clean
	make -C comm clean
	rm -f *.map $(OBJS) $(APP_BINS) $(APP_ELFS) $(APP_SIZES) $(LST)
