# Grafite/oro source release candidate — F31

This integration is based on the working `feature/bmw-6wb` tree at
`4a2d127898d5bfd4bbd86a75b3a2e922cbf6af16`. The later remote diagnostic commits
are preserved as history at the user's request; none of their temporary test
backend, build entries, APIs, startup hooks or documentation is used.

The supplied kit is the styling/geometry reference. Its integration instructions
are preserved under `src/cluster/grafite_oro/INTEGRAZIONE-CODEX.md`, but the user's
subsequent specification overrides its legacy-renderer fallback and static exit
example selection. The interrupted implementation was inspected and copied to
`/workspace/6wb-integration-interrupted.zip` before it was reworked. The older
untracked `experimental/grafite_oro` work is independent and remains untouched.

## Architecture

* The AA receiver, functional decoder, MOST/DSI client, host/tag/payload matching,
  finite timeouts and navigation lifecycle remain in place. Maps and Waze feed
  the same existing NavView callback; there is no app-specific graphics path.
* The kit's POD tables preserve all 6612 reference points, semantic colours,
  widths, contour closure and painter order. They use C++98-compatible loading
  instead of enum classes, initializer-list vectors, range loops or `std::move`.
* Standard maneuvers use the matching kit geometry. Roundabouts are generated
  from real direction/angle; the eight exit examples are not selected in the
  production AA mapping. Left/right/straight lane composites retain the kit's
  approved outlines where they describe the data exactly. Richer lane data uses
  the same generator with per-direction geometry and highlighting.
* There is NO graphics fallback to `bmwManeuver`/the old monochrome renderer in
  Hud6wbDisplay, including the next-maneuver bitmap. Those legacy helpers remain
  available for existing 6WA/reference tests, not as a replacement drawing path.
* Holes are retained geometrically: concentric rings become matched wedges;
  general even-odd shapes are decomposed at vertices and edge intersections.
  No guessed background colour is used to paint over holes. Strokes use existing
  native polylines, with integer-width quantization and <=99 points per encoded
  call; longer lines are split with a shared endpoint. Native cap/join appearance
  remains a vehicle validation item.
* Long pictures are emitted as ordered chunks of <=128 calls, with the existing
  port idle gate between chunks. No incomplete picture is replaced. Predicted
  buffer state is committed after the last chunk is accepted. A chunk's local
  dispatch must finish before the next is submitted; acknowledgement handling
  remains asynchronous. Work is bounded (2048 commands per scene, 4096 per frame).
* `SetLineColor` (218), already an allowed/validated protocol setter, joins the
  existing paced-graphics whitelist. Its wire format, correlation and errors
  are unchanged; it does not add a blocking acknowledgement barrier.

## Roundabouts

The angle convention is exactly the existing backend's:

```
global bearing = 180 + angle   (clockwise)
global bearing = 180 - angle   (counter-clockwise)
point = (500 + radius*sin(bearing), 475 - radius*cos(bearing))
```

Entry is from the bottom. Number and angle have distinct presence flags. The
ring uses kit dimensions (outer radius 315, inner radius 139), graphite fill,
17-unit gold edges and 49-unit white guidance. The exit branch, curved guidance
connection and arrow follow the real angle, including intermediate angles.
The exit number is rendered independently in the ring opening. Missing angle
means a generic circulation/entry diagram without a fabricated radial exit.
Enter variants do not invent an exit even if an angle is supplied.

AA codes 0..46 have explicit mappings. Unrecognized codes are reported and do
not silently become straight arrows. Ferry side variants present in the kit
are not assigned unverified AA numeric codes 47..50.

## Lanes

Every physical lane retains its ordered list of `(shape, highlighted)` entries,
including shape zero. Supported directions cover AA shapes 1..9: straight,
slight/normal/sharp left/right and U-turns. Unknown/empty directions reserve the
lane's place. No direction is collapsed to another category. Highlighted paths
are painted after non-highlighted paths to keep shared stems legible. The lane
strip remains 283x40; the new data structure never uses the perspective plane.
An empty list clears/hides the strip. More than eight lanes or an unsupported
shape is an explicit representation error, not a partial/shifted lane row.

## Central surface — one verified A/B pair

All central indications now use **only 0x1F/0x20**, regardless of family,
distance, bar visibility, optional angle or exit number. This includes arrows,
roundabouts, U-turns, ferry/train ferry and destination flags. The experimental vector input
is now 233x206, using `(0,0)..(232,205)` without a central fit margin; measured
C04 dimensions remain 221x158 through separate profile fields. The generator
and its source vector geometries are unchanged. Uniform fitting scales the
geometry/strokes to the full input, without inventing a new perspective warp.
See [6WB_VECTOR_COORDINATES.md](6WB_VECTOR_COORDINATES.md) for pinned JayJay
evidence and the distinction between experimental coordinates and C04 data.

The renderer always draws into the inactive central buffer, then uses the
existing SwitchVisibility A/B operation. It never selects a separate central
plane. The old kit policy labels remain asset metadata; only Hidden controls
whether a central picture is present. They no longer select handles.

The lateral distance bar retains exactly its previous calculation and visibility;
it no longer influences selection of the central drawing surface. Lane strip,
RoadIcon buffers and native text handles are unchanged.

There is no optional flat pair, additional surface check or file-based handle
override. The normal eight C04 checks cover the container, both central buffers,
both RoadIcon buffers, both lane buffers and the bar. Cached validation, ordered
chunk submission and transport matching remain unchanged.

## Compiled provisional colours — no configuration file required

Colour roles no longer depend on `/opt/aa/6wb-kit.palette`:

| Semantic role | Compiled index |
|---|---:|
| Kit Gold / edges / contour | 2 |
| Graphite / infill | 14 |
| Gray / secondary | 14 |
| Black / checker infill | 14 |
| Kit White / guidance / ActiveSelection | 3 (provisional, actual colour not assumed) |

The three source constants are `COLOR_CONTOUR`, `COLOR_INFILL` and
`COLOR_ACTIVE_SELECTION` in `src/cluster/hud6wb_kit.cpp`. These are the user's
explicit provisional assignments, not new colour measurements. Main maneuvers,
roundabouts and lanes use the same mapping. Native bitmap RGB is a separate
codec path and does not interpret HUDTerminal indices as RGB.

The backend no longer opens or parses `6wb-kit.palette` for any purpose.
An absent, obsolete or malformed file cannot affect calibration, colours,
central handles or normal drawing. Neither `flat.a` nor `flat.b` exists in the
active profile. The main pair is fixed to 1F/20 and rejected if changed in code
to other handles. Unsupported AA codes/invalid geometry still report errors;
there is no unrelated-style fallback.

At each navigation start, the built-in palette presentation runs before normal
guidance. See [6WB_PALETTE_CALIBRATION.md](6WB_PALETTE_CALIBRATION.md) for the
4x5 filled-rectangle ordering, monotonic ten-second deadline and source disable switch.

The kit's swatch helper remains available, and the startup presentation now
shows 20 filled rectangles (21B + 21D type 03) through the existing display worker. No separate test
program, IPC service or startup executable is installed. The provisional colour
assignments come from the user, not RGB estimates from photographs.
RGB remains usable for the existing RoadIcon bitmap encoding; its pixels use
kit styling and the existing ABGR/RLE32 codec and 4096-byte cap.

## Tests and remaining validation

Executed for the expanded-grid update without compilation (24 Python checks in total):

* 4 expanded-geometry contracts (full input, unchanged C04 expectation, no 6WA
  canvas restriction and no unrelated protocol/NAV changes).
* 5 single-surface regression contracts: fixed pair, no file/flat dependencies,
  unchanged calibration lifecycle, geometry/colour/decoder/transport invariants and eight C04 checks.
* 7 calibration source/data checks: 4x5 filled grid, adjacent 21B/21D pairs,
  unique indices, reference frames for transparent samples, bounds, gaps,
  63-call budget and isolated production diff.
* 8 Python source/data tests: all 6612 imported coordinates and 585 styles,
  explicit enum mapping, no exit-example selection, angular mirror convention,
  uniform fits against the supplied geometry at 180x77, 220x157, 232x205 and 282x39, with zero/two-unit margins,
  per-direction lane preservation, one central pair with unchanged bar logic, no legacy draw calls,
  build lists, C++98 lexical subset, delimiters, command limits and atomic append.
* Git whitespace/diff review; no new binaries, logs or credentials in the change.

These are source/data checks. The independent Python maths checks do NOT execute
C++. `tests/test_hud6wb_kit.cpp` prepares deterministic runtime tests for angles
1,15,90,127,180,225,270,359,360 in both directions, generic rotatories, 2..6 lanes,
per-branch highlighting, bounds, palette zero/unmapped cases and the shared central pair. Additional renderer
cases exercise all 46 supported visible AA codes with and without distance, and
the adapter tests both 1F and 20 plus rejection of 21.
Existing renderer tests use explicitly synthetic colour indices, never car
measurements. No C++ tests were compiled or executed, and no QNX compiler/SDK
was searched or configured.

Still required on Windows/F31:

1. Compile and run appropriate C++ checks on the user's environment; verify the
   resulting ARM ELF separately. Compilation and ABI compatibility are unproven.
2. Observe the provisional colour roles and verify normal 1F/20 A/B rendering
   of each maneuver family after calibration; no separate central pair is needed.
3. Validate native joins, subpixel stroke quantization, graphite/gold contrast,
   optional black checker cells and Comfort/Sport/Eco presentation.
4. Validate real exit-angle convention and joins for clockwise/counter-clockwise
   rotatories, intermediate angles, missing angles and exit numbers.
5. Validate 2..6 lanes, mixed highlights, obliques/sharp turns, U-turns and unknown
   positions using the actual decoded data. Only Google data was captured in
   the available JSONL; Waze operation is not claimed as hardware-tested.
6. Check chunked update ordering, central A/B swaps,
   ending navigation/reconnecting and absence of a stale/doubled picture.


No new executable, installation, automatic startup, GitHub release or positive
vehicle test is produced by this source candidate.
