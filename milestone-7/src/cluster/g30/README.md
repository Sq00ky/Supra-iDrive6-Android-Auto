# G30 independent Android Auto map

The G30 backend receives Android Auto's separate instrument-cluster H.264
stream on channel 2 / display ID 2. The dashboard retains its existing channel 6
and hardware decoder. Related discussion: [issue #226](https://github.com/Sq00ky/Supra-iDrive6-Android-Auto/issues/226).

`bridge.cpp` runs inside `nbt-aad`, handling stream negotiation, the live setting,
bounded buffering and acknowledgements. `player.cpp` builds a separate process
that decodes with OpenH264 and presents native PiP. `native/` implements the
BMW control/Guidance exchanges, resource profile, Screen output and metadata.
`inspect.cpp` builds a read-only Screen diagnostic. The iDrive setting and its
labels live in `../../../rhmiapp/`; decoder sources live in
`../../../third_party/openh264/` (paths relative to `milestone-7/src/cluster/g30`).

## Source build

With a licensed QNX SDP 6.5 ARMv7LE toolchain and the normal project link
libraries, run from `milestone-7`:

```sh
make clean
make G30_PIP=1 nbt-aad rhmiapp g30-player g30-inspect
```

Override `QCC` and `QNX_AR` when necessary. Outputs are `nbt-aad`,
`rhmiapp/libNbtAad.so`, `build/g30/player` and `build/g30/inspect`.
The plugin also requires its matching manifest, XML, text and image bundles
from `rhmiapp/`. These targets compile source; packaging and installation are
the user's deployment responsibility. Run `make clean` before switching
between enabled and ordinary builds. Ordinary builds leave G30 PiP disabled.

## Runtime integration

The feature requires an externally supplied supervisor. The daemon uses QNX
`spawn()` to invoke `/bin/sh` with the script path and owning daemon PID. The
default script path is `/opt/aa/g30-pip/service.sh`; set
`NBT_G30_CLUSTER_SERVICE` to select another path. The source-directory layout
does not change that existing on-car path. A missing supervisor is logged and
the cluster stream remains unavailable.

The supervisor must validate the target firmware and Screen managers, preserve
the factory window, own the temporary producer address `160.48.199.120` on
`vlan73`, launch `player --service`, and monitor the owning daemon. It must
terminate/wait for its player and remove only its own address and lock on exit
or parent death. Player diagnostics should survive failures. This lifecycle is
required for a working deployment; copying the daemon alone is insufficient.
The supervisor is deployment-specific and is maintained outside this repository.

The iDrive setting **Show map in instrument cluster** applies live. Off releases
PiP and restores the dial; on requests a fresh map stream. The helper can remain
available for the same Android Auto session. Video stays in RAM; only the
two-byte preference at `/var/opt/aa/kombi-video` persists.

## Output contract and validation

The verified precursor deployment used a G30 with NBT EVO **007.039.008 /
ID5_2111**. Its native profile reserves an 800×480 resource, colour depth 0 and
packaging 1, for a 620×313 sink with selection 1. The player owns Screen window
268444456 on display index 1 and retains factory window 1056. It writes the
84-byte BGRA metadata row and picture counter, then fits the map to 522×313
with 49-pixel sidebars at content origin `(0,1)`.

The user verified map playback and off/on dial restoration while the dashboard
stayed connected; diagnostics recorded 840 posts across two map sessions.
That evidence belongs to the precursor deployment. This public source layout
has not been installed in the car. Host regressions live in
[tests/g30](../../../tests/g30/README.md) and run with `make check-g30` or the
existing `make check`. They exercise the real decoder, player loop and session
parser with simulated native services, including disconnects, rapid toggles,
malformed input and partial-startup cleanup. Native build records and
deployment evidence remain outside this repository.
Concurrent factory navigation is not arbitrated; custom HUD video and other
firmware/cluster combinations are not validated.
