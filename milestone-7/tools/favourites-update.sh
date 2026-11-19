#!/bin/sh
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
# Existing-installation update only. Does not remount filesystems, stop AA or reboot.
set -eu
fail() { echo "ERROR: $*" >&2; exit 1; }
mode=${1:---check}
case "$mode" in --check|--install) ;; *) fail 'Use --check or --install';; esac
cd "$(dirname "$0")"
[ "$(uname -s)" = QNX ] || fail 'Requires a QNX NBT Evo head unit.'
case "$(uname -m)" in arm*|ARM*) ;; *) fail 'Requires ARM QNX.';; esac
[ "$(id -u)" = 0 ] || fail 'Run in the head-unit root shell.'
plugin=/opt/hmi/ID5/data/ro/Plugins/NbtAad
[ -f /opt/aa/nbt-aad ] && [ -f "$plugin/NbtAad.manifest" ] || fail 'Existing NBT-AAD installation required.'
[ -x /opt/sys/bin/LayerManagerControl ] || fail 'LayerManagerControl is unavailable.'
[ -f /opt/sys/lib/libilmClient.gpp.so.1 ] || fail 'Required LayerManager ABI is unavailable.'
geometry=$(/opt/sys/bin/LayerManagerControl get surface 256 2>/dev/null) || fail 'Cannot read native display geometry.'
# 1440x540 is the 10.25" panel (G30 and friends). The Supra J29 has the 8.8" panel, which reports
# 1280x480, or 1180x480 with 40 px side insets (see FIXES.md). The strip height is derived from the
# reported height at run time, so both are supported; anything else is still refused.
echo "$geometry" | grep -qE 'original size: x=(1440, y=540|1280, y=480|1180, y=480)' || fail 'This prebuilt package is validated for 1440x540, 1280x480 and 1180x480 NBT Evo ID6 displays only.'
# CRC and byte count detect corrupted transfers without requiring a SHA utility on QNX.
# Verify SHA256SUMS on the computer before copying the package to the car.
[ -s PAYLOAD.cksum ] || fail 'Missing payload checksums.'
while read -r crc bytes file; do
    case "$file" in nbt-aad|nbt-aa-favourites|release/libNbtAad.so|release/NbtAad.manifest|release/NbtAad.xml|release/NbtAad_ALL_Texts.zip|release/NbtAad_ALL_Images.zip) ;; *) fail 'Unexpected payload path.';; esac
    [ -f "$file" ] || fail "Missing $file"
    set -- $(cksum "$file")
    [ "$1" = "$crc" ] && [ "$2" = "$bytes" ] || fail "Checksum mismatch: $file"
done < PAYLOAD.cksum
[ "$(wc -l < PAYLOAD.cksum | tr -d ' ')" = 7 ] || fail 'Expected seven payload files.'
echo 'Payload and basic platform checks passed. See README for hardware compatibility.'
[ "$mode" = --install ] || exit 0
# The caller must stop AA normally. Do not force-stop other processes or restart the HU.
pidin ar > /tmp/nbt-favourites-pids.$$
trap 'rm -f /tmp/nbt-favourites-pids.$$' EXIT HUP INT TERM
if awk '$2 ~ /(^|\/)(nbt-aad|nbt-aa-favourites)$/ {found=1} END {exit !found}' /tmp/nbt-favourites-pids.$$; then
    fail 'Stop Android Auto and the favourites helper before installing.'
fi
[ -w /opt/aa ] && [ -w "$plugin" ] || fail 'Filesystem is read-only; use the upstream update preparation first.'
backup=/opt/aa/favourites-backup-$(date +%Y%m%d-%H%M%S)-$$
mkdir "$backup"
mkdir "$backup/release"
cp -p /opt/aa/nbt-aad "$backup/nbt-aad"
if [ -f /opt/aa/nbt-aa-favourites ]; then cp -p /opt/aa/nbt-aa-favourites "$backup/nbt-aa-favourites"; fi
for name in libNbtAad.so NbtAad.manifest NbtAad.xml NbtAad_ALL_Texts.zip NbtAad_ALL_Images.zip; do
    [ -f "$plugin/$name" ] || fail "Installed plugin is incomplete: $name"
    cp -p "$plugin/$name" "$backup/release/$name"
done
# Stage every file before replacing any live file. Backups are outside the plugin scanner.
cp nbt-aad /opt/aa/nbt-aad.favourites-new
cp nbt-aa-favourites /opt/aa/nbt-aa-favourites.favourites-new
cmp nbt-aad /opt/aa/nbt-aad.favourites-new || fail 'Daemon staging verification failed.'
cmp nbt-aa-favourites /opt/aa/nbt-aa-favourites.favourites-new || fail 'Helper staging verification failed.'
chmod 755 /opt/aa/nbt-aad.favourites-new /opt/aa/nbt-aa-favourites.favourites-new
for name in libNbtAad.so NbtAad.manifest NbtAad.xml NbtAad_ALL_Texts.zip NbtAad_ALL_Images.zip; do
    cp "release/$name" "$plugin/$name.favourites-new"
    cmp "release/$name" "$plugin/$name.favourites-new" || fail "Staging verification failed: $name"
    chmod 644 "$plugin/$name.favourites-new"
done
echo "Backup: $backup"
for name in libNbtAad.so NbtAad.manifest NbtAad.xml NbtAad_ALL_Texts.zip NbtAad_ALL_Images.zip; do
    mv "$plugin/$name.favourites-new" "$plugin/$name"
done
mv /opt/aa/nbt-aa-favourites.favourites-new /opt/aa/nbt-aa-favourites
mv /opt/aa/nbt-aad.favourites-new /opt/aa/nbt-aad
sync
echo 'Installed. Restart the head unit manually to load the matching plugin.'
echo "To restore: sh favourites-restore.sh '$backup'"
