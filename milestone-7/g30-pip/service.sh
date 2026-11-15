#!/bin/sh
# Installed AA map helper. Runs only for its owning daemon; pixels stay in RAM.
set -u
PATH=/bin:/usr/bin:/sbin:/usr/sbin:/opt/sys/bin:/opt/car/bin
LD_LIBRARY_PATH=/opt/mm/lib:/lib:/usr/lib:/opt/sys/lib:/opt/car/lib
export PATH LD_LIBRARY_PATH
umask 077
owner=${1:-}
case "$owner" in ''|*[!0-9]*) exit 90;; esac
test "$owner" -gt 1 && kill -0 "$owner" 2>/dev/null || exit 90
case "$0" in
    */*) cd "${0%/*}" || exit 90;;
    *) exit 90;;
esac
test -x ./player && test -x ./inspect || exit 90
test "$(uname -n)" = hu-omap || exit 90
test "$(cat /opt/hmi/ID5/data/ro/version.txt)" = 'NBTevo_ID5_2111_Release ID5_2111-540-2142A Build 28' || exit 90
ifconfig vlan73 | grep 'inet 160\.48\.199\.119 ' >/dev/null || exit 90
flags=/net/hu-jacinto/var/opt/sys
test -d "$flags" && test -r "$flags" && test -x "$flags" || exit 90
test ! -e "$flags/APIX_DRIVER_OFF" && test ! -e "$flags/APIX_DFE_1920" || exit 90
lock=/tmp/idriver-kombi-demo.lock
dir=/tmp/idriver-kombi-demo-service-$owner-$$
mkdir "$lock" || exit 91
if ! mkdir "$dir"; then rm -r "$lock"; exit 91; fi
echo "$dir" > "$lock/owner"
child=
watch=
alias_added=0
mode=video
cleanup() {
    trap '' HUP INT TERM
    if test -n "$child"; then
        : > "$dir/STOP"
        kill -TERM "$child" 2>/dev/null
        wait "$child" 2>/dev/null
        child=
    fi
    if test -n "$watch"; then kill -TERM "$watch" 2>/dev/null; wait "$watch" 2>/dev/null; watch=; fi
    alias_ok=1
    if test "$alias_added" = 1; then
        ifconfig vlan73 inet 160.48.199.120 -alias || alias_ok=0
        if ifconfig vlan73 | grep 'inet .*160\.48\.199\.120 ' >/dev/null; then alias_ok=0; fi
    fi
    if test "$alias_ok" = 1 && test "$(cat "$lock/owner" 2>/dev/null)" = "$dir"; then
        rm -f "$dir/player.log" "$dir/STOP"
        rm -r "$dir"
        rm -f "$lock/owner"
        rm -r "$lock"
    fi
    echo "AA_CLUSTER_SERVICE_CLEANUP alias_removed=$alias_ok"
    test "$alias_ok" = 1
}
trap 'cleanup; exit 130' HUP INT TERM
if ifconfig -a | grep 'inet .*160\.48\.199\.120 ' >/dev/null; then
    echo REFUSE_EXISTING_ALIAS; cleanup; exit 92
fi
if arp -n 160.48.199.120 2>/dev/null | grep -E '([0-9a-fA-F]{1,2}:){5}[0-9a-fA-F]{1,2}' >/dev/null; then
    echo REFUSE_ALIAS_NEIGHBOR; cleanup; exit 93
fi
if test "$mode" = video; then
    # A privileged context adopts only displays with no manager in this exact
    # Screen build. Require existing managers before creating that context.
    for index in 1 2; do
        set -- /dev/screen/*/dpy-"$index"
        if test "$#" != 1 || ! test -f "$1" ||
           ! grep '^window manager = [1-9][0-9]*$' "$1" >/dev/null; then
            echo REFUSE_MISSING_FACTORY_DISPLAY_MANAGER; cleanup; exit 98
        fi
    done
    ./inspect || { cleanup; exit 94; }
    set -- /dev/screen/*/dpy-2
    if test "$#" != 1 || ! test -f "$1"; then
        echo REFUSE_OUTPUT_BECAME_BUSY; cleanup; exit 97
    fi
    if ! grep '^scene = (empty)$' "$1" >/dev/null || ! grep '^windows = (none)$' "$1" >/dev/null; then
        win=$(sed -n 's/^windows = \(win-[0-9][0-9]*\)$/\1/p' "$1")
        if test -z "$win" || ! grep "^scene = $win\$" "$1" >/dev/null; then
            echo REFUSE_OUTPUT_BECAME_BUSY; cleanup; exit 97
        fi
        set -- /dev/screen/*/"$win"/"$win"
        if test "$#" != 1 || ! test -f "$1" ||
           ! grep '^id string = 1056$' "$1" >/dev/null ||
           ! grep '^display id = 2$' "$1" >/dev/null ||
           ! grep '^buffer size = 800x480$' "$1" >/dev/null ||
           ! grep '^format = SCREEN_FORMAT_RGBA8888$' "$1" >/dev/null ||
           ! grep '^destination rectangle = (0,0 800x480)$' "$1" >/dev/null; then
            echo REFUSE_UNKNOWN_SECONDARY_WINDOW; cleanup; exit 97
        fi
        echo FACTORY_WINDOW_RETAINED id=1056
    fi
fi
ifconfig vlan73 inet 160.48.199.120 netmask 255.255.255.255 alias || { cleanup; exit 95; }
alias_added=1
./player --service >> "$dir/player.log" 2>&1 </dev/null &
child=$!
(
    sleeper=
    trap 'if test -n "$sleeper"; then kill -TERM "$sleeper" 2>/dev/null; wait "$sleeper" 2>/dev/null; fi; exit 0' HUP INT TERM
    while kill -0 "$owner" 2>/dev/null && kill -0 "$child" 2>/dev/null && test ! -f "$dir/STOP"; do
        # A bounded diagnostic in RAM; no encoded video or persistent logging.
        bytes=$(wc -c < "$dir/player.log")
        if test "$bytes" -gt 1048576; then : > "$dir/player.log"; fi
        sleep 1 & sleeper=$!; wait "$sleeper"; sleeper=
    done
    kill -TERM "$child" 2>/dev/null
    sleep 8 & sleeper=$!; wait "$sleeper"; sleeper=
    kill -KILL "$child" 2>/dev/null
) &
watch=$!
wait "$child"
status=$?
child=
if test "$status" -ne 0; then
    echo "AA_CLUSTER_PLAYER_FAILED status=$status"
    cat "$dir/player.log"
fi
cleanup || status=96
trap - HUP INT TERM
exit "$status"
