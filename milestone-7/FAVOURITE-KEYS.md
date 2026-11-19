# BMW Favorite Keys

## What this adds

Lightly touching one of the eight physical BMW favourite keys shows its native
number, icon, label and selection above Android Auto. Android Auto keeps its full
picture and touch area: the bar does not shrink or resize the video. Pressing the
key still runs the BMW action. This works with native stored favourites, including
vehicle settings; it does not replace them with Android Auto shortcuts.

Under **Android Auto → Settings → BMW Favorite Keys**:

- **Show Favorites Keys Bar** enables or hides the preview.
- **bar color** offers BMW default orange, Match ambient lighting, Bronze, White,
  Orange, Blue, Green and Lilac.
- **custom color** opens a touch-operated saturation/brightness square and vertical
  hue slider, with a preview and Save/Cancel. There are no numeric input fields.

White + colour ambient schemes use their coloured component. The volume indicator
uses the same selected colour. The Media/Radio entry retains the upstream icon (unchanged).

## How the native bar is shown

The daemon observes the existing MOST favourite-key touch/status messages
(`15/0C/C02`). It publishes the selected key and a short preview lease with its
normal connection-state record. It keeps the factory HMI surface available behind
Android Auto so the HMI can render its own labels. It does not write factory UI
files or synthesize the shortcut's state.

The project-owned `nbt-aa-favourites` helper captures only the native top strip,
checks that the expected selected banner is present, and copies the strip into a
transparent Screen window above Android Auto (surface 1283). LayerManager updates
keep the native HMI behind AA and the copied strip above it. The volume indicator
stays above the strip. Camera/reverse/native-screen handovers cancel the preview.

Per-key cached frames make subsequent touches quick. Physical key press/release
edges request a bounded fresh capture. Lifting the finger schedules a final capture
while the native bar is still visible; the overlay remains eligible for **450 ms**
before hiding. This preserves refreshed toggle/checkmark state for the next touch.
An externally changed setting, such as the physical SYNC button, becomes visible
when a fresh native frame is captured; there is no separate climate-state cache
or invented checkmark update.

A failed capture retains the last valid image. Retry logic is bounded. If the
helper exits, a new touch allows one restart, avoiding an endless respawn loop.
The optional helper is owned and stopped by the daemon, rather than a factory boot
hook. Setting `NBT_FAVOURITE_PREVIEW=0` disables native preview composition.

## How colour selection works

Preferences live under `/var/opt/aa/`: `favourite-bar-colour`,
`favourite-bar-custom`, and `favourite-bar.disabled`. Ambient mode observes the
native lighting status (`C0/0A/CD0`), with a fixed initial status Get and no lighting
Set commands. A short settling interval avoids mixing a new scheme with the old
hue. The shared palette covers the single-colour and White + colour scheme IDs.
Only the native orange accents are tinted; neutral text and icons remain intact.

The colour picker is another mode of the same helper. It draws a separate Screen
surface, receives the saved HMI touch peer's datagrams while AA has released touch
ownership to native settings, saves the chosen RGB atomically and closes on Save,
Cancel, lost focus or idle timeout. It does not stop the daemon or restart the HU.

## Build and resources

This integration is based on upstream `b6934b5a058d69061aacff31274e5c019c432ec2`
(`v1.9.78-pr218`). It preserves the newer bounded video queue, audio changes,
navigation/logging settings and Version page. Favourites states are appended as
16/17, with text IDs 48–59, so the existing state order and text IDs stay intact.

Using the project's QNX SDP 6.5 build environment and the unit's existing link
libraries:

```sh
python3 milestone-7/rhmiapp/make_db.py
./build.sh milestone-7 nbt-aad nbt-aa-favourites rhmiapp
make -C milestone-7/tests check
```

The three compiled components and all five plugin files must be deployed together.
The source helper's build is included in `make all`; `make clean` removes it.
Resource regression tests check state order, duplicate IDs, translated text
separation and the bundled menu icon.

## Compatibility and validation

The earlier installed implementation was owner-tested on a BMW NBT Evo iDrive 6
with a 1440×540 touch display: all eight labels/highlights, repeated touches,
colours including mixed ambient schemes, bar toggle, custom picker and volume
colour were checked. Release capture improved the rear-climate checkmark on the
next touch. The final 450 ms timing was host-tested; its final post-restart car
confirmation was not recorded.

This latest-upstream port is separately compiled and host-tested. It has **not yet
been installed and tested in a car**. Other firmware, screen sizes and non-touch
hardware need their own validation. This is not a universal installer for all BMWs,
ID7/MGU units, Android aftermarket screens or an AA retrofit box. Factory/QNX link
libraries, credentials and vehicle captures are not included in release packages.

The latest-port validation run completed the full host suite: 83 `[PASS]` results,
including ThreadSanitizer, plus the resource consistency and isolated updater tests.
The optional 32-bit host run and protobuf schema check were skipped because this
build machine lacks the `-m32` toolchain and `protoc`. All three QNX targets built
successfully; existing signedness/initialization-order compiler warnings remain.
Update tests cover checksum corruption, wrong display geometry, a running daemon,
backup/staging/install and restoration both with and without a prior helper.

## Compiled update and rollback

`tools/package_favourites.py` builds a checksummed ZIP from the compiled files.
The supplied updater is for an existing NBT-AAD installation on the reference
1440×540 NBT Evo platform. It checks QNX/ARM, required native ABI, display geometry,
payload checksums, stopped AA/helper processes and writable installation folders.
It backs up all replaced files outside the HMI plugin directory, stages and
compares every new file before replacing live files, and leaves preferences alone.

Follow the upstream [update preparation](update.md) to enter the HU shell and make
the installation writable. Park the car, stop AA with **Force Stop**, and copy the
extracted package to a USB drive. From its directory in the head-unit shell:

```sh
sh favourites-update.sh --check
sh favourites-update.sh --install
```

The updater prints a unique `/opt/aa/favourites-backup-...` path. Keep it. Restart
the head unit manually when ready; the script does not restart it or enable SSH.
After restart, validate labels 1–8, native shortcut actions, bar visibility, picker,
ambient/manual colours, volume colour and normal camera/reverse handover.

To restore, stop AA, prepare the filesystem as above and run:

```sh
sh favourites-restore.sh /opt/aa/favourites-backup-THE-PRINTED-PATH
```

Then restart manually. Backups are retained. A power loss during replacement may
require restoring through SSH; do not interrupt the update.

## Credits and notices

NBT-AAD was developed by nanaisu and the upstream contributors; this feature builds
on their daemon, native plugin, touch and LayerManager infrastructure. Preserve
the project's [PolyForm Noncommercial license](../LICENSE.md) and source notices.

Android Auto is a Google trademark. The blue/silver arrow is attributed to Google,
from the [Wikimedia Android Auto icon](https://commons.wikimedia.org/wiki/File:Android_Auto_icon.svg).
The fixed palette references owner-documented
[BMW LED-library RGB values](https://f90.bimmerpost.com/forums/attachment.php?attachmentid=2528852&d=1613348714);
neutral display white is used rather than physical-LED white compensation.
