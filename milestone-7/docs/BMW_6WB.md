# Experimental BMW 6WB guidance over MOST HUDTerminal

This backend adds `NBT_CLUSTER=6wb` to the existing Android Auto navigation
pipeline. It targets F-series BMWs with NBT EVO and an original 6WB, initially
an F31 LCI (2018), without changing the J29 or 6WA backend defaults.
**The corrected source has not been compiled with the QNX ARM toolchain or
validated for live rendering on that car. Prior F31 captures completed the eight
size queries; this new paced-graphics revision still needs ARM build and hardware validation.**

## Implementation and provenance

The path is unchanged: Android Auto channel 10 → `NavMessages`/`NavView` →
navigation state machine → `ClusterDisplay` → `Hud6wbDisplay` → `HudTerminal`
→ `most::Client` → the existing NBT EVO DSI client. This extends Tom-Pryor's
upstream PR #134 implementation rather than importing another MOST transport.
The subsequent upstream 6WA fixes and time-based approach logic are retained.
The new 6WB selection shows guidance immediately, with full distance text;
the other selections retain their upstream approach settings.

The profile and wire-format references come from
[JayJay3071/nbt-android-auto](https://github.com/JayJay3071/nbt-android-auto),
commit `0fb9fd2759e8cea8790465cd9ed8b60504eb5f09`:
`src/android_auto/knob/nbt_cluster.cpp`,
`src/android_auto/knob/ref/most/client.cpp` and `docs/CLUSTER-HUDTERMINAL.md`.
That project's README reports operation on an F07 (2014), NBT **x86**;
this is not evidence of operation on F31/NBT EVO ARM.
The outline-arrow geometry and RLE32 encoding adapt that work. Its standalone
application, x86 dependencies, hard-coded destination and benchmark commands
are not imported. The existing PolyForm Noncommercial 1.0.0 license and Required
Notices apply; retain these attributions when distributing the adapted code.

| File | Responsibility |
| --- | --- |
| `src/cluster/hud6wb_profile.hpp` | Explicit handles, dimensions, placement and limits |
| `src/cluster/hud6wb_resources.hpp` | Query and verify documented image surfaces |
| `src/cluster/hud6wb_display.*` | Rendering, buffering, shadow logging and cleanup |
| `src/cluster/hud6wb_artwork.*` | Maneuver outlines, roundabouts, exits and lanes |
| `src/cluster/hud6wb_codec.*` | Text, distance bar, ABGR RLE32 and optional road shields |
| `tests/test_hud6wb.cpp` | Host tests with fake MOST/DSI and a synthetic cluster |

## F31 ImageGetSize capture and parser correction

A stationary F31 LCI 2018 / NBT EVO ID6 capture with NBTevo_V20153I
observed the OEM navigation frame 0x17 switch off and the terminal become ready.
One live ImageGetSize call for that handle received ResultAck with six bytes:
`00 17 01 35 01 4A` (handle 0x0017, width 309, height 330, all BE16).
The old seven-byte result (selector byte followed by dimensions) existed in
synthetic tests, without a hardware capture justifying it; it is not accepted.
The request still includes its existing selector byte.

The result is accepted only for a pending function/handle and return-routing
tag, with positive dimensions and before the original deadline. Invalid,
uncorrelated, duplicate and late results do not populate the size cache or
complete another query. A correlated ErrorAck still stops the terminal.
The registered tag is obtained from the existing MOST client; no address/tag
is hard-coded and the send format is unchanged. Other cluster drivers do not
use this strict size-result path.

The tag is routing identity, not a transaction sequence. A size query must not
be repeated for the same host/local-address/tag/handle during this terminal's
lifetime: it cannot distinguish a delayed old result from a new result. The
bounded history refuses such a retry rather than guessing. Already validated dimensions from an unchanged link/host cache
are reused without repeating queries. Revalidation after an interrupted query
batch may stop safely; there is no automatic retry.
Process restart is not proof that queued replies have disappeared.

The observed result was logged after a 3000 ms timeout. The capture lacks an
explicit dispatch timestamp, so it cannot establish the actual wire latency.
ImageGetSize now logs a diagnostic threshold at 3000 ms and keeps the same
request pending until a final experimental deadline of 5000 ms, without retry.
At or beyond 5000 ms it fails. Blocking methods (including C05 upload) retain
3000 ms; whitelisted graphics now use the asynchronous policy below.
Query dispatch/deadline, send-return duration,
valid-result elapsed time and timeout timestamps are now logged separately;
dispatch timing is not a measurement of delivery onto the MOST bus.

Software tests replay the six-byte result and exercise invalid lengths, the
unsupported seven-byte shape, zero sizes, wrong peer/function/instance/tag/handle,
duplicates, mirrored results and expiration with or without a prior timer tick.
They do not establish hardware rendering. Actual rendering and this new ARM
revision still need vehicle validation. Before the next diagnostic start, restore `NBT_HUDTERM=log` on the
vehicle: its configuration is not changed by this source update.

## Selection and shadow mode

```text
NBT_CLUSTER=6wb
NBT_HUDTERM=log
NBT_KOMBI_NAV=1
```

The existing iDrive turn-arrows setting can be used instead of
`NBT_KOMBI_NAV=1`. For 6WB only, every `NBT_HUDTERM` value except the exact
string `on`, including an unset variable, means **shadow**. It renders the
indications and logs complete intended payloads, including size queries and
bitmaps, without submitting HUDTerminal calls or acquiring send rights.
It leaves the shown-picture marker untouched. The observer still registers
and subscribes to DSI/MOST: this is not a claim of zero local IPC traffic.

`NBT_HUDTERM=on` explicitly opts into the experimental live path. It does not
bypass resource validation. No configuration files are written
by selecting this backend, and no installer is provided.

## Experimental 6WB testing without OEM prerequisites

The 6WB driver intrinsically disables OEM navigation arbitration: no preliminary
BMW route, frame-off observation, quiet wait, route-log watch or yielding on OEM
activity is required, including after reconnect. `NBT_6WB_OEM_BYPASS` is no
longer read or needed. The operator uses Android Auto exclusively during tests.
Other cluster drivers retain their existing defaults and OEM arbitration.
SHADOW still sends nothing; LIVE keeps discovery, registered addressing,
surface validation, reply correlation, query-reuse protection and finite deadlines.
No surface allocation or instance registration is added. Startup remains manual.

ImageGetSize reception logs entry into the MOST client's DSI callback, the MOST
parser timestamp, terminal callback entry and Core handling time. The first
stamp is after the vendor DSI trampoline decoded the event; it is not a bus
arrival timestamp and cannot locate latency inside the firmware/server.
The tap/logger runs between MOST parsing and terminal dispatch, so these stamps
can expose local logging/dispatch delay. Completing the size checks does not prove
that live rendering works on the car.

## Data and layout

| Role | Handle(s) | Dimensions reported by the reference |
| --- | --- | --- |
| Navigation frame | `0x17` | 309 × 330 |
| Upper images | `0x18/0x19` | 305 × 44 |
| Street, distance, unit | `0x1A/0x1B/0x1C` | Text instances |
| Main perspective drawing | `0x1F/0x20` | 221 × 158 |
| Lane images | `0x23/0x24` | 283 × 40 |
| Distance bar | `0x35` | 28 × 113 |

These are logical navigation resources, **not** the physical panel resolution
or permission to draw over the whole cluster. They are an experimental profile,
not measurements of the target F31. No critical instrument regions are targeted.
The main glyph placement below the perspective horizon needs vehicle validation.

Implemented inputs are the current AA maneuver, street, distance/unit, lane
directions/highlights, roundabout angle and exit number (when present), plus a
second-step icon only when a second AA step exists. A missing roundabout angle
does not produce an invented exit direction. No second-step distance, speed
limit, ETA or other unavailable data is synthesized. Invalid lane shapes hide
the lane row. `NBT_KOMBI_NAV_LANES=0` disables it.

Text is converted to ISO-8859-15 with bounded lengths and replacement of
unrepresentable characters. The bar uses measured distance, full at/above the
profile's 1000 m range and hidden when distance is unknown. Main, lane and upper
images have separate buffer pairs; distance-only updates do not flip them.

Road-shield bitmap generation is implemented, but inference from explicit
`A 45`/`B 45` road prefixes is disabled unless `NBT_6WB_ROAD_SHIELDS=1`.
The blue/yellow design is a German-style heuristic, not structured AA sign data
or a universal road classification. No BMW bitmap assets are included.

## Transport and fail-closed behavior

The recipient is resolved from the MOST registry key `0x8200`; `0x0101` is not
assumed. The sender and return-routing header tag come from DSI registration.
6WB calls use FBlock `82/00`, StartResultAck (`6`), `varTag=1`, `varVal=0`,
vector `txType=4` and bitmap `txType=0`. Registered header tags and request
padding are opt-ins; the existing 6WA tag/transport defaults are unchanged.

Live operation requires all of the following:

1. A registered MOST link and a valid registry host for 82/00.
2. Acknowledged positive ImageGetSize dimensions exactly matching all eight
   selected profile surfaces before drawing. Text instances are not queried.
3. Only whitelisted functions/handles. One blocking result at a time; graphical
   setters advance at the configured cadence without waiting for their answers.
   An active batch finishes in order; only a not-yet-started waiting batch may
   be replaced/merged by the existing queue policy. No automatic retries.
4. Correlated method, handle, routing tag and payload. Invalid or late results
   cannot authorize drawing. The 3000 ms size-query diagnostic threshold does
   not cancel or replace the request; its final deadline is 5000 ms.

No instance allocation, resolution switching, handle scan or guessed session
initialization is attempted. Error `0x41` is not assigned an unproven meaning
and does not trigger a reset to force access. For paced graphics, cleanup waits
for local dispatch completion, NOT wire acknowledgements, and is withheld if
the terminal is not ready. Crash/SIGKILL cleanup
cannot be guaranteed; the existing marker mechanism remains in use.

The protocol does not let the observer distinguish identical requests from
two clients with absolute certainty. Experimental 6WB testing assumes BMW
navigation remains off; OEM arbitration is disabled for this profile.

Bitmap calls (`0xC05`) contain a complete surface, format 1, one application
segment, ABGR RLE32 and padding. Payloads over 4096 bytes are omitted, never
truncated. Existing RX reassembly is reused; async bitmap transport and large
application-level TX segmentation remain disabled pending EVO evidence.
There is no live map or arbitrary full-panel drawing.

## Build and validation

No new proprietary dependency is introduced. Use the existing QNX 6.5 ARMv7LE
Makefile and a valid SDK/license, with `QNX_HOST`, `QNX_TARGET` and SDK `PATH`
configured for the actual installation:

```sh
qcc -V
cd milestone-7
make QCC="$QNX_HOST/usr/bin/qcc" TARGET_ARCH=-Vgcc_ntoarmv7le_cpp nbt-aad
file nbt-aad
readelf -h -A -l nbt-aad
```

The inherited ABI flags are `-march=armv7-a -mfpu=vfpv3 -mfloat-abi=softfp`;
the qcc C++ setting is `-Wc,-std=gnu++0x`. Target libraries remain a2i_client,
socket, ssl, crypto, screen, usbdi, ang-c++ and m, plus the existing runtime-loaded
BMW DSI library `libsys_dsi_colibry.cpp-ne.so.8` and its dependencies.
No proprietary libraries or binaries are added. Do not use `make all` when
only the daemon is wanted: it also builds plugins and tools.

Host verification uses Linux g++, fake transports and synthetic inputs:

```sh
make -C milestone-7/tests check
```

The new tests cover text/RLE round trips, geometry, payloads, buffer/visibility
sequences, mismatched resources, registered DSI headers, replies, timeouts,
terminal arbitration in isolated transport tests, reconnection and zero HUDTerminal submissions in default/log
mode. Existing 6WA, J29 golden and MOST tests are retained. C++98 syntax checks
and an ARM Makefile dry-run are additional checks, not a QNX compilation.

Preparation results on Linux: 71 normal host tests (70 C++ and one Python) passed, including the new
6WB test and the unchanged 6WA/J29 regressions. The aggregate `check` command
returned failure because this container disallows `setarch -R`; the same
ThreadSanitizer lifecycle binary passed when run directly with
`TSAN_OPTIONS=halt_on_error=1`. The schema and 32-bit host tests were skipped
because `protoc` and a `-m32` toolchain are absent. C++98 syntax checks passed
for the changed cluster/MOST implementation, as did `git diff --check`.
Make/CMake source lists were checked and `make -n nbt-aad` included all new
sources. Neither `cmake` nor `qcc` is available here: CMake configuration and
QNX ARM compilation have not been completed.

Outstanding vehicle work: verify F31 handles/dimensions, route-end visibility,
return routing, method reply formats, access permissions, palette/font,
perspective placement and bitmap behavior. Until then, this contribution is
implemented and host-tested software, **not vehicle-confirmed 6WB support**.

## Optional low-overhead MOST/DSI timing recorder

`NBT_6WB_DIAG=1`, read once at manual startup with `NBT_CLUSTER=6wb`, enables
only instrumentation. No timing, matching, renderer or protocol setting is
changed. With it absent/off there is no exporter thread, file or diagnostic
clock read. The statically allocated ring remains reserved in the executable.
The existing `NBT_MOST_LOG`/stdout logs are unchanged; for a low-overhead capture,
use `NBT_MOST_LOG=0` rather than a mass USB trace. Respect the launcher's 15-setting
limit. No vehicle configuration is edited by this feature.

The recorder has 2048 fixed events. Producers read CLOCK_MONOTONIC in nanoseconds
(actual resolution is platform-dependent), fill fixed metadata, and use only
pthread_mutex_trylock to append. No allocation, printf, file I/O, sleeps or
blocking lock acquisition is added to the producer. If busy, the event is lost;
contention losses are NOT counted. Ring eviction is counted as `overwritten`.
A short exporter copy of at most 128 events can cause trace drops, never an
application wait. OS scheduling and clock/mutex implementation costs still exist;
this is not a real-time latency guarantee.

A dedicated thread exports CSV to `/dev/shmem/nbt-aad-6wb-diag.csv`, independently
of stdout and USB logging. The file is fresh per daemon start, limited to 8 MiB,
and the worker stops exporting at the cap. Collect it before another Start or
NBT shutdown. Normal daemon exit retains its existing `_exit` behavior: it does
not wait for the diagnostic thread; the last buffered events/footer may be lost.
A crash/reset also loses the RAM file. Missing file can indicate exporter/open
failure; absence of events is not evidence of absence of delay. The recorder
is lossy and must not be used as a protocol truth source.

CSV `ns` is phase entry, `end_ns` is completion (equal/nearly equal for point
events); divide differences by 1000000 for milliseconds. `serial` orders storage,
not wire transmission: completed spans are stored after their nested events.
A handler that never returns has no completed span; absence of a span cannot
exclude a hung callback.
Group spans by `thread` and `context`, and sort by `ns` to reconstruct overlap.
Context is a process-local object address, not a stable vehicle identifier.

| Phase | Meaning |
|---|---|
| 1 | Core dispatch, before registered source/tag substitution |
| 2 / 3 | Before / after send-lock acquisition, actual encoded MOST metadata |
| 4 / 5 | Before / immediately after vendor stream send |
| 6 | Entire DSI trampoline span, entry captured before lookup/decoding/logging |
| 7 | MOST parser span, only ImageGetSize/SetVisibility |
| 8 | MOST message processing span, includes tap/logging and route dispatch |
| 9 | Entry into HudTerminal before its mutex |
| 10 | Entry into Core::onAnswer |
| 11 | Existing timeout decision, no additional recovery |
| 12 | Individual router handler span, context is consumer user pointer |
| 13 | Unambiguously correlated graphics acknowledgement |
| 14 | Graphics command unconfirmed after its telemetry window; nonfatal |
| 15 | Valid graphics reply with reused/ambiguous key; request=0 |
| 16 | Explicit graphics error; request=0 when no safe attribution exists |
| 17 | Graphics acknowledgement tracking abandoned on link/failure/drop |
| 18 | Batch dispatch complete; request is the internal batch generation |
| 19 | Unambiguous graphics reply after the old 3000 ms threshold |

`request` is an internal Core dispatch counter where available. At answer entry
it is the current candidate's counter, NOT proof of successful matching; zero
means unavailable/no pending request. Other stages are correlated offline by
peer/function/handle/tag/time, not by inventing a transaction ID. The registered
MOST tag is routing identity, not a unique transaction sequence. `dsi_event`
and `dsi_seq` are vendor event/sequence fields only when available. Handle is
meaningful for HUDTerminal; for other traffic it is only the first two bytes.
No payload beyond that prefix is recorded. Both C04 and 204 replies, including
late ones, reach the recorder before validation decides their outcome.
Asynchronous graphics go through the ledger (phases 13–17/19), not CoreAnswer
phase 10. A phase 18 event confirms local dispatch, never successful display.

2→3 isolates send-lock waiting; 4→5 isolates the vendor send call. Overlapping
6/8/12 spans on the response's callback thread show earlier handlers occupying
that thread. 6→7→9→10 separates trampoline work, tap/dispatch and terminal-lock
waiting. Long time before phase 6 remains ambiguous: the vendor's internal
receiver/queue, IPC/server, MOST transport and device cannot be separated by
this recorder. That requires QNX thread/IPC tracing or server/vendor-side
instrumentation. Never interpret dispatch→6 as measured device execution time.


## Paced graphics revision (baseline d2c3d8511918001e8cc321bd24bda58bb5b5219d)

Only `NBT_CLUSTER=6wb` enables `Config::pacedGraphics`. The SHADOW and LIVE
selection is unchanged; SHADOW still acquires no send rights. Other cluster
profiles keep their existing per-answer scheduler, timeouts and OEM policy.

Classification is explicit, not a blanket fire-and-forget switch:

| Methods | Scheduling | Reason |
|---|---|---|
| C04 ImageGetSize | Blocking; diagnostic 3000 / final 5000 ms | Dimensions are needed and all eight surfaces must match before drawing |
| 204, 20E, 210, 219, 21B, 21D, 220, 227 | Ordered, paced, asynchronous acknowledgements | Existing setters/drawing methods used this way by JayJay; no returned value is needed to build the next command |
| C05 bitmap upload | Blocking, existing 3000 ms | TX0 upload completion remains a barrier before TX4 visibility changes; cross-channel ordering is not hardware-verified |
| Other permitted methods | Existing blocking behavior | No evidence justifying asynchronous scheduling |

The reference is JayJay `12c8411618acd3eea5fb0a5ff1a57ef1c87761e0`,
`nbt_cluster.cpp` send/onMsg. The cadence is initially 15 ms, set by
`Config::graphicIntervalMs` in the 6WB setup in `nav_core.cpp`; change that field
for experiments, not a new vehicle env option. It is a minimum between local
send starts, not a physical bus-spacing guarantee. The existing sender's
monotonic condition variable schedules the next call; no sleep was added to
main or DSI callbacks. No transport timeout, wire format, TX4 vector or TX0
bitmap selection changed. JayJay's newer TX1 bitmap choice is a separate
hardware question and is not imported here.

A graphics command is entered in a bounded ledger before send. Local send
success permits the next ordered command; an immediate failure remains fatal.
A batch becomes idle when all its commands have been dispatched successfully
and blocking results have arrived. **Idle/batchesDone no longer means all
asynchronous graphics were acknowledged.** Buffer swap and cleanup retain their
place in the batch. The renderer waits for dispatch completion before advancing
to the next batch. New Maps views remain dirty while a batch runs and are
rendered afterwards; the Core also retains its existing one-waiting-batch policy.
Previously validated sizes are cached until link/host invalidation, as before.
No instances are created/released and no OEM navigation prerequisite is added.

The graphics ledger retains commands/outcomes for 8000 ms, independently of the
old 3000 ms graphic deadline. Missing confirmation then increments
`graphicsUnconfirmed` and removes telemetry only; it does not fail, retry or
extend a transport deadline. Health summaries distinguish dispatched,
acknowledged, unconfirmed, ambiguous, late and abandoned. CSV phases above
provide per-command timestamps without new per-answer text logging.

**Conservative identity policy:** host/local address/client tag/function/handle
is a routing key, not a transaction ID. ResultAck must also match the exact
method payload (with the existing text terminator exception). ErrorAck must
contain handle + at least one opaque error byte. Only a key issued once during
this Core lifetime has a unique command candidate. A second use makes that key
permanently ambiguous, even if the first call was answered or its 8 s ledger
entry expired. This deliberately prevents an arbitrarily late duplicate from
being assigned to a later identical command. It also means many repeated setters
and polygons cannot have individual confirmations counted; their valid replies
are telemetry, not proof of completion. `graphicsUnconfirmed` includes such
unattributable commands and must not be read as measured packet loss.

A unique ErrorAck for the latest batch is fatal and drops unsent work. An error
from an older/abandoned generation, a reused key or an Error(F) without a proven
handle remains visible in error counters/CSV but never fails an unrelated newer
batch. Duplicate results do not double-count accepted answers. Invalid
peer/tag/handle/function/type/payload does not complete anything. An 8 s window
cannot create a transaction nonce; process restart or registration/address reuse
also cannot prove all old wire traffic disappeared.

Storage is bounded: 1024 live ledger entries (at 15 ms, fewer than 535 normally
fit in 8 s) and 512 distinct registration keys retained for this Core lifetime.
Exhaustion is an explicit structural failure, never silent eviction/retry or an
unbounded queue. Tracking is abandoned on disconnect/failure; discovery and
mandatory resource validation still gate resumed drawing.

Host tests: `make -C tests run-test_hud6wb_async`. Payloads are synthetic;
eight size-query timing pairs and the 3365 ms graphics latency come from the
provided CSV. Tests cover pacing/order, full resource initialization/cache,
missing/late/duplicate/out-of-order replies, explicit and ambiguous errors,
old batches, Maps updates while sending, mandatory timeouts, local-send failure,
clock wrap, bounded storage and the real sender thread. Existing 6WA/J29 suites
remain required. None of these tests proves rendering on the BMW.
