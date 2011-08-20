#!/bin/bash

TOPDIR=`pwd`
INDIR="$TOPDIR/doc"
OUTDIR=/tmp/doxywww
GITREV=`./git-version-gen .tarball-version`

[ -f "$OUTDIR" ] || mkdir "$OUTDIR"

for MOD in core gsm vty codec; do
	TGTDIR="$OUTDIR/libosmo$MOD/$GITREV"
	mkdir -p "$TGTDIR"
	cp -R "$INDIR/$MOD/"* "$TGTDIR/"
done
