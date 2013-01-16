
# this is not really used as we don't do 'make install'. You can still specify
# it in case you _want_ to manually 'make install' the target libosmocore.
CROSS_INST_PREFIX ?= /usr/local/stow/osmocom-bb/arm-2010.09

# this is the host tuple of your cross-toolchain
CROSS_HOST ?= $(shell which arm-elf-gcc >/dev/null 2>&1 && echo arm-elf || echo arm-none-eabi)

# this is the prefix of your cross-toolchain programs
CROSS_TOOL_PREFIX=$(CROSS_HOST)-

TOPDIR=$(shell pwd)

all: libosmocore-target nofirmware firmware mtk-firmware
nofirmware: layer23 osmocon gsmmap

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


HOST_APPS=osmocon gsmmap layer23

define HOST_APP_template
.PHONY: $(1)

host/$(1)/configure: host/$(1)/configure.ac
	cd host/$(1) && autoreconf -i

host/$(1)/Makefile: host/$(1)/configure
	cd host/$(1) && ./configure $(HOST_CONFARGS) $$(HOST_$(1)_CONFARGS)

$(1): host/$(1)/Makefile
	make -C host/$(1)
endef

$(foreach app,$(HOST_APPS), \
	$(eval $(call HOST_APP_template,$(app))))


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
	make -C target/firmware $@
	make -C target/firmware -f Makefile.mtk $@

distclean:
	rm -rf shared/libosmocore/build-target
	make -C host/layer23 $@
	make -C host/osmocon $@
# 'firmware' also handles 'mtk-firmware'
	make -C target/firmware $@
