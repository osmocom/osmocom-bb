
# this is not really used as we don't do 'make install'. You can still specify
# it in case you _want_ to manually 'make install' the target libosmocore.
CROSS_INST_PREFIX ?= /usr/local/stow/osmocom-bb/arm-2010.09

# this is the host tuple of your cross-toolchain
CROSS_HOST ?= $(shell which arm-elf-gcc >/dev/null 2>&1 && echo arm-elf || echo arm-none-eabi)

# this is the prefix of your cross-toolchain programs
CROSS_TOOL_PREFIX=$(CROSS_HOST)-

TOPDIR=$(shell pwd)

all: libosmocore-target nofirmware firmware mtk-firmware

nofirmware: layer23 osmocon trxcon gprsdecode virtphy

libosmocore-target: $(LIBOSMOCORE_PREFIX)/lib/libosmocore.a

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


.PHONY: layer23
layer23: host/layer23/layer23

host/layer23/configure: host/layer23/configure.ac
	cd host/layer23 && autoreconf -i

host/layer23/Makefile: host/layer23/configure
	cd host/layer23 && ./configure $(HOST_CONFARGS)

host/layer23/layer23: host/layer23/Makefile
	make -C host/layer23

.PHONY: gprsdecode
gprsdecode: host/gprsdecode/gprsdecode

host/gprsdecode/configure: host/gprsdecode/configure.ac
	cd host/gprsdecode && autoreconf -i

host/gprsdecode/Makefile: host/gprsdecode/configure
	cd host/gprsdecode && ./configure $(HOST_CONFARGS)

host/gprsdecode/gprsdecode: host/gprsdecode/Makefile
	make -C host/gprsdecode

.PHONY: firmware
firmware: libosmocore-target
	make -C target/firmware CROSS_COMPILE=$(CROSS_TOOL_PREFIX)

.PHONY: mtk-firmware
mtk-firmware: libosmocore-target
	make -C target/firmware -f Makefile.mtk CROSS_COMPILE=$(CROSS_TOOL_PREFIX)


clean:
	make -C host/layer23 $@
	make -C host/osmocon $@
	make -C host/gprsdecode $@
	make -C host/virt_phy $@
	make -C host/trxcon $@
	make -C target/firmware $@
	make -C target/firmware -f Makefile.mtk $@

distclean:
	make -C host/layer23 $@
	make -C host/osmocon $@
	make -C host/gprsdecode $@
	make -C host/virt_phy $@
	make -C host/trxcon $@
# 'firmware' also handles 'mtk-firmware'
	make -C target/firmware $@
