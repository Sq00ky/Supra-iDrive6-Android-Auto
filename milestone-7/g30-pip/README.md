# G30 Android Auto cluster map

Experimental support for Android Auto's independent instrument-cluster map,
verified on one G30 with NBT EVO **007.039.008 / ID5_2111**. Related discussion:
[upstream issue #226](https://github.com/Sq00ky/Supra-iDrive6-Android-Auto/issues/226).

This branch starts from upstream `ade1b27` (`usb-and-settings`), the revision
used for the car test. It adds a native player and the iDrive setting
**Media/Radio → Android Auto → Settings → Show map in instrument cluster**.
The setting applies live: off restores the dial, on requests the independent
map again, and the main Android Auto connection stays open.

## Implementation

- Android Auto channel 2 / display ID 2 negotiates **800×480 H.264**, with four
  outstanding media packets. Channel 6 retains the original dashboard path.
- NBT decodes the cluster stream using a single-threaded OpenH264 decoder.
  Every reference picture is decoded; identical pictures avoid a Screen post.
  A bounded RAM queue and deferred acknowledgements provide backpressure.
- Native BMW SOME/IP control and Guidance calls acquire a **620×313** sink,
  selection **1**, with an **800×480** resource (colour depth 0, packaging 1).
  The producer temporarily owns `160.48.199.120` on `vlan73`; the cluster is
  `160.48.199.96`. Those are internal car-network addresses, not SSH targets.
- The player owns secondary Screen window `268444456` on display index 1.
  It writes the factory **84-byte BGRA metadata**, an incrementing big-endian
  picture counter, and map pixels at `(0,1)`. The 800×480 AA picture is fitted
  to 522×313 with 49-pixel sidebars inside the 620×313 content rectangle.
- A helper launched with QNX `spawn()` supervises the player, checks firmware,
  Screen managers, geometry and alias ownership, and removes only its own
  temporary alias on session shutdown. Map-off releases the PiP resources;
  the helper stays available until the Android Auto session ends.
- Video stays in RAM. Only the two-byte preference at
  `/var/opt/aa/kombi-video` is persisted. The arrow renderer is suspended while
  map output is selected or its cleanup remains pending.

Factory window 1056 is retained. Concurrent factory route guidance is not
arbitrated; cancel BMW route guidance and turn HUD navigation off for testing.
Other firmware/cluster combinations and custom HUD video are not validated.

## Build

Use a licensed **QNX SDP 6.5** environment targeting ARMv7LE, with the same
link libraries as the ordinary upstream build. The OpenH264 decoder source
subset and its BSD license are included in `third_party/openh264`.

```sh
cd milestone-7
make g30-pip
```

Set `QCC` and `QNX_AR` if the SDK tools are not on PATH. The build cleans stale
objects before enabling the feature and writes these nine installation files
to `g30-pip/build/`:

| Files | Target directory |
|---|---|
| `nbt-aad` | `/opt/aa/` |
| `player`, `inspect`, `service.sh` | `/opt/aa/g30-pip/` |
| `libNbtAad.so`, `NbtAad.manifest`, `NbtAad.xml`, `NbtAad_ALL_Texts.zip`, `NbtAad_ALL_Images.zip` | `/opt/hmi/ID5/data/ro/Plugins/NbtAad/` |

The ordinary `make nbt-aad` path keeps the feature disabled. After a G30 build,
run `make clean` before switching back. `rhmiapp/make_db.py` regenerates the
tracked text/image bundles when labels change; they must match `NbtAad.xml`.

## Installation scope

This source branch does not supply a new one-click installer. Use the project's
established backup/install workflow with the complete nine-file output above.
Keep the car parked and awake, Force Stop Android Auto before replacement,
back up the prior daemon/plugin/helper, and remount the firmware volume
read-only afterward. The executable, library, player and inspector need execute
permissions; install `service.sh` as executable as well.

Restart iDrive after replacing the settings plugin. Connect the phone, open a
compatible Android Auto navigation app and switch the cluster-map setting on.
A failed helper needs an off/on retry after its cause is corrected; it does not
continuously restart. Check `/dev/shmem/nbt-aad.log` for helper startup/failure.
While running, player diagnostics are under
`/tmp/idriver-kombi-demo-service-*/player.log`.

## Validation

The precursor implementation was built with native QNX SDP 6.5 and tested in
the parked car on November 15, 2026. The user confirmed the map appears and
that off/on restores the normal dial and map while the dashboard stays
connected. Captured diagnostics recorded **840 native posts across two map
sessions** and acknowledged Guidance cleanup. This is evidence for that
car/session, not a sustained driving-performance or multi-car compatibility
claim.

This source branch relocates the same protocol, decoder, supervisor and
settings code into this repository. The dashboard video manager is unchanged
from the tested upstream base. Build/test results for this layout are recorded
in `validation.json`; previous physical validation is identified separately.

Host regressions require Python 3, Clang with ASan/UBSan, FFmpeg, pkg-config and
OpenSSL headers:

```sh
python3 milestone-7/g30-pip/check.py
```

They cover routing, acknowledgement credits, bounded queues, persistent settings,
real AASession dispatch, native Screen/metadata contracts, Guidance cleanup,
H.264 pixel output and eight supervisor lifecycle/error cases. The H.264 inputs
are generated test patterns. They do not access a vehicle.

The prior car failures were a worker-thread `fork()` rejected by QNX 6.5 and
a missing `dirname` utility in the NBT shell environment. This branch includes
the working `spawn()` launch and shell-built-in directory resolution.

## Licensing

The project and derived Guidance code retain PolyForm Noncommercial 1.0.0:
see the repository's `LICENSE.md` and existing notices. OpenH264 is BSD-2-Clause;
its original source notices and license are retained. SDK/license files,
private captures and personal recordings are not included in this feature.
