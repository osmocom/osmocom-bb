
# this is not really used as we don't do 'make install'. You can still specify
# it in case you _want_ to manually 'make install' the target libosmocore.
CROSS_INST_PREFIX ?= /usr/local/stow/osmocom-bb/arm-2010.09

# this is the host tuple of your cross-toolchain
CROSS_HOST ?= $(shell which arm-elf-gcc >/dev/null 2>&1 && echo arm-elf || echo arm-none-eabi)

# this is the prefix of your cross-toolchain programs
CROSS_TOOL_PREFIX=$(CROSS_HOST)-

TOPDIR=$(shell pwd)

all: libosmocore-target nofirmware firmware mtk-firmware
nofirmware: layer23 osmocon trxcon gsmmap virtphy

libosmocore-target: shared/libosmocore/build-target/src/.libs/libosmocore.a

shared/libosmocore/configure: shared/libosmocore/configure.ac
	cd shared/libosmocore && autoreconf -fi

shared/libosmocore/build-target:
	mkdir $@

shared/libosmocore/build-target/Makefile: shared/libosmocore/configure shared/libosmocore/build-target
	cd shared/libosmocore/build-target && ../configure \
			--host=$(CROSS_HOST) --enable-embedded --disable-shared \
			--disable-tests ac_cv_header_sys_select_h=no \
			--disable-tests ac_cv_header_sys_socket_h=no \
		CFLAGS="-Os -ffunction-sections -I$(TOPDIR)/target/firmware/include -nostartfiles -nodefaultlibs"

shared/libosmocore/build-target/src/.libs/libosmocore.a: shared/libosmocore/build-target/Makefile
	cd shared/libosmocore/build-target && make


.PHONY: osmocon
osmocon: host/osmocon/osmocon

host/osmocon/configure: host/osmocon/configure.ac
	cd host/osmocon && autoreconf -i

host/osmocon/Makefile: host/osmocon/configure
	cd host/osmocon && ./configure $(HOST_CONFARGS)

host/osmocon/osmocon: host/osmocon/Makefile
	make -C host/osmocon

.PHONY: virtphy
virtphy: host/virt_phy/virtphy

host/virt_phy/configure: host/virt_phy/configure.ac
	cd host/virt_phy && autoreconf -i
	
host/virt_phy/Makefile: host/virt_phy/configure
	cd host/virt_phy && ./configure $(HOST_CONFARGS)

host/virt_phy/virtphy: host/virt_phy/Makefile
	make -C host/virt_phy

.PHONY: trxcon
trxcon: host/trxcon/trxcon

host/trxcon/configure: host/trxcon/configure.ac
	cd host/trxcon && autoreconf -i

host/trxcon/Makefile: host/trxcon/configure
	cd host/trxcon && ./configure $(HOST_CONFARGS)

host/trxcon/trxcon: host/trxcon/Makefile
	make -C host/trxcon


.PHONY: gsmmap
gsmmap: host/gsmmap/gsmmap

host/gsmmap/configure: host/gsmmap/configure.ac
	cd host/gsmmap && autoreconf -i

host/gsmmap/Makefile: host/gsmmap/configure
	cd host/gsmmap && ./configure $(HOST_CONFARGS)

host/gsmmap/gsmmap: host/gsmmap/Makefile
	make -C host/gsmmap


.PHONY: layer23
layer23: host/layer23/layer23

host/layer23/configure: host/layer23/configure.ac
	cd host/layer23 && autoreconf -i

host/layer23/Makefile: host/layer23/configure
	cd host/layer23 && ./configure $(HOST_CONFARGS)

host/layer23/layer23: host/layer23/Makefile
	make -C host/layer23


.PHONY: firmware
firmware: libosmocore-target
	make -C target/firmware CROSS_COMPILE=$(CROSS_TOOL_PREFIX)

.PHONY: mtk-firmware
mtk-firmware: libosmocore-target
	make -C target/firmware -f Makefile.mtk CROSS_COMPILE=$(CROSS_TOOL_PREFIX)


clean:
	make -C shared/libosmocore/build-target $@
	make -C host/layer23 $@
	make -C host/osmocon $@
	make -C host/gsmmap $@
	make -C host/virt_phy $@
	make -C host/trxcon $@
	make -C target/firmware $@
	make -C target/firmware -f Makefile.mtk $@

distclean:
	rm -rf shared/libosmocore/build-target
	make -C host/layer23 $@
	make -C host/osmocon $@
	make -C host/gsmmap $@
	make -C host/virt_phy $@
	make -C host/trxcon $@
# 'firmware' also handles 'mtk-firmware'
	make -C target/firmware $@
