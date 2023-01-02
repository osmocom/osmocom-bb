#!/bin/sh
# jenkins build helper script for osmocom-bb.  This is how we build on jenkins.osmocom.org
#
# environment variables:
# * WITH_MANUALS: build manual PDFs if set to "1"
# * PUBLISH: upload manuals after building if set to "1" (ignored without WITH_MANUALS = "1")
#

set -ex

base="$PWD"
deps="$base/deps"
inst="$deps/install"
export deps inst

osmo-clean-workspace.sh

mkdir "$deps" || true

# TODO: ask whether fail is expected, because osmocom-bb build succeeds?
#"$deps"/libosmocore/contrib/verify_value_string_arrays_are_terminated.py $(find . -name "*.[hc]")

export PKG_CONFIG_PATH="$inst/lib/pkgconfig:$PKG_CONFIG_PATH"
export LD_LIBRARY_PATH="$inst/lib"

osmo-build-dep.sh libosmocore "" ac_cv_path_DOXYGEN=false
osmo-build-dep.sh gapk

set +x
echo
echo
echo
echo " =============================== OsmocomBB ==============================="
echo
set -x


# building those sub-projects where 'distcheck' is known-working
for dir in gprsdecode layer23 trxcon virt_phy; do
	cd $base/src/host/$dir
	autoreconf -fi
	./configure
	make distcheck
done

# TODO: make sure 'distcheck' passes also for these
for dir in gsmmap osmocon; do
	cd $base/src/host/$dir
	autoreconf -fi
	./configure
	make
done

# Build and publish manuals
if [ "$WITH_MANUALS" = "1" ]; then
	make -C "$base/doc/manuals"
	make -C "$base/doc/manuals" check

	if [ "$PUBLISH" = "1" ]; then
		make -C "$base/doc/manuals" publish
	fi
fi

# Test 'maintainer-clean'
for dir in gprsdecode layer23 gsmmap osmocon trxcon virt_phy; do
	cd "$base/src/host/$dir"
	make maintainer-clean
done

# Build the firmware (against the local copy of libosmocore)
cd "$base/src"
make firmware

# TRX Toolkit unit tests
cd "$base/src/target/trx_toolkit"
python3 -m unittest discover -v

osmo-clean-workspace.sh
