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
`rhmiapp/libNbtAad.so`, `build/g30/kombi-player` and `build/g30/inspect`.
The plugin also requires its matching manifest, XML, text and image bundles
from `rhmiapp/`. These targets compile source; packaging and installation are
the user's deployment responsibility. Run `make clean` before switching
between enabled and ordinary builds. Ordinary builds leave G30 PiP disabled.

Production always uses the QNX SDK's POSIX, thread and time APIs; no custom
SDK-selection define is needed. `G30_PIP=1` is the feature switch. The older
QNX ARM compiler's atomic workaround is selected by compiler/platform macros.
Host regressions explicitly select `NBT_G30_HOST_TEST` and obtain mock API
declarations from `tests/g30/`, which is absent from production include paths.
NBT's Screen header is not part of the base SDP 6.5 installation, so
`native/native_screen.hpp` retains the car-verified Screen binding and property
values. This is independent of the host test stubs.

## Runtime integration

Install `nbt-aad` and `kombi-player` in `/opt/aa/`, plus the matching five RHMI
plugin files in `/opt/hmi/ID5/data/ro/Plugins/NbtAad/`. The release consists of
`nbt-aad`, `kombi-player` and `Releases.zip`. See the
[installation guide](../../../../install.md#g30-cluster-map-build).

The daemon uses QNX `spawn()` to launch `kombi-player --supervise OWNER_PID` directly.
The player's compiled supervisor checks the live network and Screen layout,
acquires the temporary producer address `160.48.199.120` on `vlan73`, and starts
a decoding child using the same executable. Inspection is built in as
`kombi-player --inspect`; no additional installed helper or shell script is required.
The supervisor monitors both the owning daemon and the child. Shutdown sends
TERM, escalates only its own child after eight seconds, reaps it, and removes
its own address and lock. The daemon drains both child output streams into its current log with a
`[kombi-player]` prefix. Successful playback and failures remain in
`/dev/shmem/nbt-aad.log` by default; the existing USB logging, RAM cap and
logging-off controls apply. No separate player log file is created. Failed
address cleanup retains the lock to prevent a competing start.

Firmware version is logged for diagnostics. There is no exact-version
allowlist. The live checks still require the OMAP node, expected internal
network addresses, existing Screen managers, an 800×480 secondary output,
and either an empty output or the known factory window 1056 with matching
geometry. Accepting a different firmware string does not establish support
for every firmware or cluster.

`NBT_G30_CLUSTER_PLAYER` can override the executable path. The explicit legacy
`NBT_G30_CLUSTER_SERVICE` override remains available for existing deployment
integrations, but ordinary installations do not set it. Remove old
`NBT_G30_CLUSTER_*` overrides when upgrading to the default player integration.

The iDrive setting **Show map in instrument cluster** applies live. Off releases
PiP and restores the dial; on requests a fresh map stream. The helper can remain
available for the same Android Auto session. Video stays in RAM; only the
two-byte preference at `/var/opt/aa/kombi-video` persists.

Service lifetime follows the owning AA connection, with a separate two-second
deadline for each Guidance transaction. Slow startup does not consume a shared
ten-second service timer. Standalone trials retain their 65-second limit.
If cancellation interrupts a reply, the player preserves partial bytes and
finishes that exact transaction before sending cleanup. Cleanup ignores the
stop signal but remains bounded; malformed or unrelated replies fail closed.

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
