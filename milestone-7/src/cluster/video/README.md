# Independent Android Auto cluster video

The shared video backend receives Android Auto's separate instrument-cluster H.264
stream on channel 2 / display ID 2. The dashboard retains its existing channel 6
and hardware decoder. Related discussion: [issue #226](https://github.com/Sq00ky/Supra-iDrive6-Android-Auto/issues/226).

`bridge.cpp` runs inside `nbt-aad`, handling stream negotiation, the live setting,
bounded buffering and acknowledgements. `player.cpp` builds a separate process
that submits cluster H.264 to a second NBT mm-renderer context and presents its output on PiP. `native/` implements the
BMW control/Guidance exchanges, resource profile, Screen output and metadata.
`inspect.cpp` builds a read-only Screen diagnostic. The iDrive setting and its
labels live in `../../../rhmiapp/`.

## Code map

Start with `bridge.cpp` for the daemon side and `player.cpp` for the decoder
side. Vehicle constants are in `../profiles/`; the single reference graph is
in `../guidance/graph_template.hpp`. The local socket between them carries framed H.264 in RAM; it is separate
from the native BMW services used to acquire the cluster display.

| Files | Responsibility |
| --- | --- |
| `manifest.hpp` | Clear the cluster channel's advertised margins and insets while preserving every protobuf message length and all other channels. |
| `router.hpp`, `credits.hpp` | Track focus, stream generations, keyframe recovery and deferred phone acknowledgements. Late completions from an old stream must not acknowledge a new one. |
| `encoded_queue.hpp`, `protocol.hpp` | Bound queued H.264 data and define the local daemon-to-player framing. |
| `hardware_decoder.cpp`, `hardware_session.hpp`, `hardware_pixels.hpp`, `native/renderer_api.hpp` | Native hardware decoder, scaled Screen readback, PiP packing and factory API bindings. |
| `hardware_capture.hpp` | Capture worker and one-picture mailbox. Input sends every encoded reference frame while copying/resizing runs independently. Joins capture before releasing renderer/Screen objects. |
| `supervisor.cpp`, `supervisor.hpp` | Own the player process, temporary producer address and exclusive lease. Reap the player before releasing the address. |
| `player_service.hpp`, `recovery.hpp` | Restart a failed helper after its supervisor exits; bound retry backoff and socket progress deadlines. |
| `process.hpp`, `service_spawn.hpp`, `service_log.hpp` | Spawn with mapped descriptors and relay both child output streams to the daemon's current log. |
| `../video_settings.hpp` | Read and atomically replace the persistent two-byte map preference. |
| `native/control.hpp`, `native/native_methods.inc`, `native/wire.hpp` | Exchange control messages and Guidance transactions, including partial replies and bounded cancellation. |
| `native/pip.hpp`, `native/presentation.hpp`, `native/screen_output.hpp` | Coordinate resource acquisition, frame presentation and cleanup. |
| `../profiles/g30.hpp`, `../profiles/video.hpp` | Define and select the verified output geometry, addresses and metadata. |
| `../guidance/graph_template.hpp` | Single captured graph shared with the turn-instruction renderer. |
| `native/native_screen.hpp`, `native/qnx_abi.hpp` | Bind the NBT Screen interface and QNX platform APIs. |
| `atomic.hpp`, `native/atomic.hpp` | Provide the atomic operations supported by the older QNX ARM compiler. |
| `inspect.cpp` | Expose the standalone read-only Screen diagnostic. |
| `bridge.hpp` | Declare the daemon's integration hooks. |

The regression tests in `../../../tests/cluster_video/` mirror these boundaries. Their
README describes the failure and recovery scenarios covered by each program.

## Source build

With a licensed QNX SDP 6.5 ARMv7LE toolchain and the normal project link
libraries, run from `milestone-7`:

```sh
make CLUSTER_PROFILE=g30 nbt-aad rhmiapp kombi-player cluster-inspect
```

Set `QCC` to override the compiler path. Outputs are `nbt-aad`,
`rhmiapp/libNbtAad.so`, `build/cluster/g30/kombi-player` and `build/cluster/g30/inspect`.
The plugin also requires its matching manifest, XML, text and image bundles
from `rhmiapp/`. These targets compile source; packaging and installation are
the user's deployment responsibility. Daemon objects are separated by profile, so switching profiles requires no
clean. The default `CLUSTER_PROFILE=j29` retains the existing turn-instruction
backend without PiP. Unsupported profile names fail at build time.

Production always uses the QNX SDK's POSIX, thread and time APIs; no custom
SDK-selection define is needed. `CLUSTER_PROFILE=g30` selects the verified video-capable profile. The older
QNX ARM compiler's atomic workaround is selected by compiler/platform macros.
Host regressions explicitly select `NBT_CLUSTER_HOST_TEST` and obtain mock API
declarations from `tests/cluster_video/`, which is absent from production include paths.
NBT's Screen header is not part of the base SDP 6.5 installation, so
`native/native_screen.hpp` retains the car-verified Screen binding and property
values. This is independent of the host test stubs.

### Hardware decoder

The G30 `kombi-player` target uses the factory mm-renderer and IVA-HD for the
cluster stream. If the second context fails, the player reports the failure
and follows the existing cleanup and recovery path.

The native backend connects to the existing factory renderer. It owns only
`kombi_hw_<PID>` and its private mdev input/group, retaining the main decoder.
The car-tested dual-decode probe supplies the setup parameters and input
message framing. Screen is asked to scale the entire decoded 800×480 window
directly into a 522×313 BGRA pixmap. The player copies its rows into the existing
620×313 content rectangle with 49-pixel opaque sidebars; it performs no CPU
interpolation. Readback is limited to 15 per second. This is
not a zero-copy pipeline. No foreign decoder buffer is mapped or assumed to
be a stable front buffer.

The input thread feeds mdev; a separate worker reads and packs scaled pictures. The
player's `poll_picture` only takes the newest completed picture without waiting for
Screen. The mailbox holds one pending picture and uses two reusable RAM buffers;
old pending pictures can be replaced, but encoded reference frames are never
dropped by this worker. Small progress counters are mutex-protected; no lock
spans readback or fitting. Capture cadence is measured from completion, so a
slow copy cannot cause immediate repeated captures. Shutdown joins the worker
before destroying renderer/Screen resources, and reset discards pending pixels.
`AA_HW_FRAME` records `capture=522x313 scaling=screen`, `readback_us` (including
the requested native resize) and `pack_us` (row copies/padding). Geometry,
stride and format are checked before reading pixels. The decoder's padded
896×576 allocation and 800×480 viewport stay unchanged, as does the final
800×480 PiP backing and metadata row. Native scaling quality, actual acceleration
and frame rate require a physical firmware test; the request is not a claim
that a particular GPU/scaler engine was selected.

Output is polled independently of encoded input, so an asynchronously decoded
last frame can appear while the phone is quiet. First output has a five-second
grace interval; input IPC has a 250 ms deadline and Screen readback has a
one-second IPC deadline. Quiet, unchanged pictures remain valid afterwards.
On child exit, the supervisor uses a bounded helper to remove only that
reaped child's renderer context. Failed cleanup retains the exclusive lease.

`AA_HW_READY`, `AA_HW_INPUT`, `AA_HW_FRAME` and `AA_CLUSTER_POST` distinguish
setup, accepted input, copied pixels and PiP posting. None alone proves
physical visible output or sustained hardware throughput. Host peers test the
contract and failure paths; actual IVA-HD/NV12 output still requires a car
test.

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

`NBT_CLUSTER_PLAYER` can override the executable path. The explicit legacy
`NBT_CLUSTER_SERVICE` override remains available for existing deployment
integrations, but ordinary installations do not set it. Generic `NBT_CLUSTER_*` overrides take precedence over the older
`NBT_G30_CLUSTER_*` names, which remain accepted for existing installations.

The iDrive setting **Show map in instrument cluster** applies live. Off releases
PiP and restores the dial; on requests a fresh map stream. The helper can remain
available for the same Android Auto session. Video stays in RAM; only the
two-byte preference at `/var/opt/aa/kombi-video` persists.

### HUD selection and automatic recovery

With **Show map in instrument cluster** enabled, the player follows the G30
DisplaySink selection. Selection 1 displays a 620×313 cluster frame on plane 1.
Selection 0 displays a 277×160 HUD frame on plane 0. Both use the same 800×480
PiP resource, Guidance handle and Screen window. A selection change posts the
latest picture immediately, even when the phone has stopped sending frames.

Android Auto supplies the secondary map at 800×480. During service discovery
the G30 requests 200 DPI for that display. Adjust `NBT_PIP_MAP_DPI` in
`/opt/aa/nbt-aad.env` to change the layout on both destinations. Use 160
for the original scale or 240 for larger labels. Restart the daemon and
reconnect the phone after a change. The main display uses `NBT_VID_DPI`.

For HUD, the player fits the active picture to 267×160 with five black
pixels on each side. The first
visible row is y=1. Row 0 uses the factory HUD template captured on firmware
007.039.008. Its configuration and context values come from that session.
Turning the map preference off releases the resource for either destination.

The existing process supervisor and keyframe recovery handle local transport
failures together. The daemon asks its supervisor to stop the old player, waits
for cleanup and process exit, then retries after 1, 2, 4, 8, 16 and at most 30
seconds. Thirty seconds of helper uptime resets the backoff. The map preference
survives the phone's focus-release response, so a replacement decoder can request
a fresh stream and IDR without an off/on toggle. Ending AA or disabling the
setting prevents automatic restart. Missing binaries, incompatible layout,
ownership conflicts and failed cleanup remain blocked until intervention;
the daemon never starts a competing owner to bypass them.

There is no frame-cadence watchdog: a stationary AA map can legitimately remain
silent. Socket transfers instead have a three-second idle deadline, reset by
byte progress, and a fifteen-second total bound per transfer. An incomplete
access unit is discarded with its connection, never spliced into a new stream.
Slow decode, presentation and control operations are logged with
`AA_CLUSTER_SLOW stage=...` in the shared log to help diagnose future stalls.

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
[tests/cluster_video](../../../tests/cluster_video/README.md) and run with `make check-cluster-video` or the
existing `make check`. They exercise the player loop, native renderer protocol and session
parser with controlled renderer and Screen peers, including disconnects, rapid toggles,
malformed input and partial-startup cleanup. Native build records and
deployment evidence remain outside this repository.
The HUD route still needs physical validation. Concurrent factory navigation and other
firmware/cluster combinations remain untested.
