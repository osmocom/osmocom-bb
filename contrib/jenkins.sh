#!/usr/bin/env bash
export PATH="$PATH:$HOME/osmo-ci/scripts"

set -ex

autoreconf --install --force
./configure --enable-static
$MAKE $PARALLEL_MAKE
$MAKE distcheck \
  || cat-testlogs.sh
