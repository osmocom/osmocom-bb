#!/bin/sh -e
TOPDIR="$(realpath $(dirname "$0")/..)"
GIT_VERSION="$("$TOPDIR"/src/shared/libosmocore/git-version-gen .tarball-version)"

copy_with_latest_and_version() {
	local marker="$TOPDIR"/.copy-done

	if [ -e "$marker" ]; then
		return
	fi

	echo "Copying binaries with "-latest" and "-$GIT_VERSION" appended..."

	# Example paths:
	# * src/target/firmware/board/se_k2xx/hello_world.highram.elf
	# * src/target/firmware/board/se_k2xx/hello_world.highram.bin
	cd "$TOPDIR"/src/target/firmware/board
	for ext in bin elf; do
		for file in */*."$ext"; do
			without_ext="${file%.*}"
			cp -v "$file" "$without_ext-latest.$ext"
			cp -v "$file" "$without_ext-$GIT_VERSION.$ext"
		done
	done

	touch "$marker"
}

prepare_ssh_cmd() {
	if ! [ -d "$TOPDIR"/osmo-ci ]; then
		# Get known_hosts from osmo-ci
		git -C "$TOPDIR" clone -q --depth=1 https://gerrit.osmocom.org/osmo-ci
	fi
	export SSH_COMMAND="ssh -o UserKnownHostsFile=$TOPDIR/osmo-ci/contrib/known_hosts -p 48"
}

ssh_mkdir_p() {
	$SSH_COMMAND binaries@ftp.osmocom.org -- mkdir -p -v /downloads/home/binaries/"$1"
}

upload_board_latest() {
	echo "[$BOARD] Uploading latest files"

	ssh_mkdir_p web-files/osmocom-bb/firmware/"$BOARD"/latest

	rsync \
		--archive \
		--verbose \
		--compress \
		--delete \
		--rsh "$SSH_COMMAND" \
		"$BOARD"/*-latest.bin \
		"$BOARD"/*-latest.elf \
		binaries@ftp.osmocom.org:web-files/osmocom-bb/firmware/"$BOARD"/latest/
}

upload_board_versioned() {
	echo "[$BOARD] Uploading $GIT_VERSION files"

	ssh_mkdir_p web-files/osmocom-bb/firmware/"$BOARD"/all

	rsync \
		--archive \
		--verbose \
		--compress \
		--rsh "$SSH_COMMAND" \
		--exclude "$BOARD/*-latest.{bin,elf}" \
		"$BOARD"/*-*-*.bin \
		"$BOARD"/*-*-*.elf \
		binaries@ftp.osmocom.org:web-files/osmocom-bb/firmware/"$BOARD"/all/
}

upload_boards_all() {
	cd "$TOPDIR"/src/target/firmware/board
	for BOARD in *; do
		if [ -z "$(find "$BOARD" -name '*.elf')" ]; then
			echo "[$BOARD] has no elf files, skipping"
			continue
		fi
		upload_board_latest
		upload_board_versioned
	done
}

copy_with_latest_and_version
prepare_ssh_cmd
upload_boards_all
