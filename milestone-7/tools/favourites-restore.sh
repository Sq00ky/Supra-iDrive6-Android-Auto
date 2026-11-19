#!/bin/sh
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
set -eu
fail() { echo "ERROR: $*" >&2; exit 1; }
[ "$(uname -s)" = QNX ] && [ "$(id -u)" = 0 ] || fail 'Requires the head-unit root shell.'
backup=${1:-}
case "$backup" in /opt/aa/favourites-backup-*) ;; *) fail 'Supply the backup path printed by the updater.';; esac
[ -d "$backup" ] || fail 'Backup not found.'
backup=$(cd "$backup" && pwd -P)
case "$backup" in /opt/aa/favourites-backup-*) ;; *) fail 'Backup must stay under /opt/aa.';; esac
plugin=/opt/hmi/ID5/data/ro/Plugins/NbtAad
[ -f "$backup/nbt-aad" ] || fail 'Incomplete backup.'
for name in libNbtAad.so NbtAad.manifest NbtAad.xml NbtAad_ALL_Texts.zip NbtAad_ALL_Images.zip; do
    [ -f "$backup/release/$name" ] || fail "Missing backup: $name"
done
pidin ar > /tmp/nbt-favourites-pids.$$
trap 'rm -f /tmp/nbt-favourites-pids.$$' EXIT HUP INT TERM
if awk '$2 ~ /(^|\/)(nbt-aad|nbt-aa-favourites)$/ {found=1} END {exit !found}' /tmp/nbt-favourites-pids.$$; then
    fail 'Stop Android Auto and the favourites helper before restoring.'
fi
[ -w /opt/aa ] && [ -w "$plugin" ] || fail 'Filesystem is read-only.'
cp -p "$backup/nbt-aad" /opt/aa/nbt-aad
if [ -f "$backup/nbt-aa-favourites" ]; then
    cp -p "$backup/nbt-aa-favourites" /opt/aa/nbt-aa-favourites
elif [ -f /opt/aa/nbt-aa-favourites ]; then
    # Preserve it for diagnosis, but prevent the old daemon from launching it.
    mv /opt/aa/nbt-aa-favourites /opt/aa/nbt-aa-favourites.restored-away.$$
fi
for name in libNbtAad.so NbtAad.manifest NbtAad.xml NbtAad_ALL_Texts.zip NbtAad_ALL_Images.zip; do
    cp -p "$backup/release/$name" "$plugin/$name"
done
sync
echo 'Previous files restored. Restart the head unit manually.'
