
# this is not really used as we don't do 'make install'. You can still specify
# it in case you _want_ to manually 'make install' the target libosmocore.
CROSS_INST_PREFIX=/usr/local/stow/osmocom-bb/arm-elf

# this is the prefix of your cross-toolchain programs
CROSS_TOOL_PREFIX=arm-elf-

TOPDIR=$(shell pwd)
OSMOCORE_CONFIGURE_ENV= LIBOSMOCORE_LIBS=$(TOPDIR)/shared/libosmocore/build-host/src/.libs/libosmocore.a \
                        LIBOSMOVTY_LIBS=$(TOPDIR)/shared/libosmocore/build-host/src/vty/.libs/libosmovty.a \
	 	    	LIBOSMOCORE_CFLAGS=-I$(TOPDIR)/shared/libosmocore/include \
	 	    	LIBOSMOVTY_CFLAGS=-I$(TOPDIR)/shared/libosmocore/include

all: libosmocore-host libosmocore-target layer23 osmocon dietlibc firmware

libosmocore-host: shared/libosmocore/build-host/src/.libs/libosmocore.la

shared/libosmocore/build-host:
	mkdir $@

shared/libosmocore/configure: shared/libosmocore/configure.in
	cd shared/libosmocore && autoreconf -i

shared/libosmocore/build-host/Makefile: shared/libosmocore/configure shared/libosmocore/build-host
	cd shared/libosmocore/build-host && ../configure

shared/libosmocore/build-host/src/.libs/libosmocore.la: shared/libosmocore/build-host/Makefile
	cd shared/libosmocore/build-host && make


libosmocore-target: shared/libosmocore/build-target/src/.libs/libosmocore.a

shared/libosmocore/build-target:
	mkdir $@

shared/libosmocore/build-target/Makefile: shared/libosmocore/configure shared/libosmocore/build-target
	cd shared/libosmocore/build-target && ../configure \
			--host=arm-elf-linux --disable-vty --enable-panic-infloop \
			--disable-shared --disable-talloc --disable-tests \
		CC="$(CROSS_TOOL_PREFIX)gcc" CFLAGS="-Os -ffunction-sections -I../../../../target/firmware/include"

shared/libosmocore/build-target/src/.libs/libosmocore.a: shared/libosmocore/build-target/Makefile
	cd shared/libosmocore/build-target && make


.PHONY: osmocon
osmocon: host/osmocon/osmocon

host/osmocon/configure: host/osmocon/configure.ac
	cd host/osmocon && autoreconf -i

host/osmocon/Makefile: host/osmocon/configure
	cd host/osmocon && $(OSMOCORE_CONFIGURE_ENV) ./configure

host/osmocon/osmocon: host/osmocon/Makefile libosmocore-host
	make -C host/osmocon


.PHONY: layer23
layer23: host/layer23/layer23

host/layer23/configure: host/layer23/configure.ac
	cd host/layer23 && autoreconf -i

host/layer23/Makefile: host/layer23/configure
	cd host/layer23 && $(OSMOCORE_CONFIGURE_ENV) ./configure

host/layer23/layer23: host/layer23/Makefile libosmocore-host
	make -C host/layer23

.PHONY: dietlibc
dietlibc:
	make -C target/dietlibc

.PHONY: firmware
firmware: libosmocore-target
	make -C target/firmware CROSS_COMPILE=$(CROSS_TOOL_PREFIX)


clean:
	make -C shared/libosmocore/build-host $@
	make -C shared/libosmocore/build-target $@
	make -C host/layer23 $@
	make -C host/osmocon $@
	make -C target/dietlibc $@
	make -C target/firmware $@

distclean:
	rm -rf shared/libosmocore/build-host
	rm -rf shared/libosmocore/build-target
	make -C host/layer23 $@
	make -C host/osmocon $@
	make -C target/dietlibc clean # always distclean
	make -C target/firmware $@
