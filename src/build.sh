#!/bin/bash

# this is not really used as we don't do 'make install'. You can still specify
# it in case you _want_ to manually 'make install' the target libosmocore.
CROSS_INST_PREFIX=/usr/local/gnuarm-4.0.2/arm-elf

# this is the prefix of your cross-toolchain programs
CROSS_TOOL_PREFIX=arm-elf-

TOPDIR=`pwd`

# autoreconf libosmocore if needed
if [ ! -f shared/libosmocore/configure ]; then
	(cd shared/libosmocore && autoreconf -i)
fi

echo "building the target (ARM) version of libosmocore"
cd "$TOPDIR/shared/libosmocore"
if [ ! -d build-target ]; then
	mkdir build-target
fi
cd build-target
export CC="${CROSS_TOOL_PREFIX}gcc"
export LD="${CROSS_TOOL_PREFIX}ld"
export RANLIB="${CROSS_TOOL_PREFIX}ranlib"
if [ ! -f Makefile ]; then
	../configure --host=arm-elf-linux --disable-shared --prefix="$CROSS_INST_PREFIX"
fi
make
unset CC
unset LD
unset RANLIB

echo "building the host (X86) version of libosmocore"
cd "$TOPDIR/shared/libosmocore"
if [ ! -d build-host ]; then
	mkdir build-host
fi
cd build-host
if [ ! -f Makefile ]; then
	../configure
fi
make

echo "building the target firmware "
cd "$TOPDIR/target/firmware"
make

echo "building osmocon"
cd "$TOPDIR/host/osmocon"
make

echo "READY!"
