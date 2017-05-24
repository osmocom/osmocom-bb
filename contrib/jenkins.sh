#!/bin/sh

set -ex

base="$PWD"
deps="$base/deps"
inst="$deps/install"
export deps inst

mkdir "$deps" || true
rm -rf "$inst"

osmo-build-dep.sh libosmocore "" ac_cv_path_DOXYGEN=false

# TODO: ask whether fail is expected, because osmocom-bb build succeeds?
#"$deps"/libosmocore/contrib/verify_value_string_arrays_are_terminated.py $(find . -name "*.[hc]")

export PKG_CONFIG_PATH="$inst/lib/pkgconfig:$PKG_CONFIG_PATH"
export LD_LIBRARY_PATH="$inst/lib"

set +x
echo
echo
echo
echo " =============================== OsmocomBB ==============================="
echo
set -x

cd src/host/layer23
autoreconf -fi
./configure
make
