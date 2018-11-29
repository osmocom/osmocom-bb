#!/bin/sh -e
# Find OSMO_GSM_MANUALS_DIR and print it to stdout. Print where it was taken from to stderr.

# Find it in env, pkg-conf and ../../../osmo-gsm-manuals
RET="$OSMO_GSM_MANUALS_DIR"
if [ -n "$RET" ]; then
	RET="$(realpath $RET)"
	echo "OSMO_GSM_MANUALS_DIR: $RET (from env)" >&2
else
	RET="$(pkg-config osmo-gsm-manuals --variable=osmogsmmanualsdir 2>/dev/null || true)"
	if [ -n "$RET" ]; then
		echo "OSMO_GSM_MANUALS_DIR: $RET (from pkg-conf)" >&2
	else
		RET="$(realpath $(realpath $(dirname $0))/../../../osmo-gsm-manuals)"
		echo "OSMO_GSM_MANUALS_DIR: $RET (fallback)" >&2
	fi
fi

# Print the result or error message
if [ -d "$RET" ]; then
	echo "$RET"
else
	echo "ERROR: OSMO_GSM_MANUALS_DIR does not exist!" >&2
	echo "Install osmo-gsm-manuals or set OSMO_GSM_MANUALS_DIR." >&2
	exit 1
fi
