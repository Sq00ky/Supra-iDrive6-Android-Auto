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

## Plane choice — exact user-confirmed bar rule

`kitBarState()` contains the OLD bar calculation, with the same distance fallback,
range and 0..255 value. Both the 227 payload and 204 visibility use that state.
The user's explicit answer chose **visibility**, not value<255:

* `DistanceBased` arrow + visible bar => perspective plane.
* Any other case => flat plane.
* Roundabouts, ferry/train ferry and destination are always FlatOnly.
* Unknown is Hidden and removes the previous central graphic.

IMPORTANT: in the existing renderer the bar is visible whenever a maneuver
 distance can be obtained, including >=1000 m where its value is saturated at
255. Therefore a known distant maneuver ALSO selects perspective. This is the
consequence of reusing visibility exactly; it is not a new proximity threshold.
A different near/far rule would need a new explicit requirement.

The source kit emits 2D vectors; it does not supply a calibrated 3D camera,
inverse projection or separate perspective artwork. This RC fits the vectors
uniformly into the chosen input rectangle and uses the terminal's transformation
on the perspective plane. **A custom calibrated perspective representation
beyond that hardware transformation is NOT implemented or verified.** It cannot
be claimed from the supplied kit or the available F31 logs. Perspective
compensation must not be fabricated from a photograph.

## Compiled provisional colours and surface configuration

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

The old file remains optional **only for surface-pair configuration**. Its
colour entries are ignored. Supported surface keys are `perspective.a`,
`perspective.b` (default 1F/20) and `flat.a`, `flat.b` (default unset).
Missing/invalid colour files cannot block colour initialization. Missing flat
surface mapping can still block a FlatOnly scene; that is a separate unresolved
mapping issue. Invalid geometry and unknown maneuver codes are still reported,
not silently drawn in an unrelated style.

At each navigation start, the built-in palette presentation runs before normal
guidance. See [6WB_PALETTE_CALIBRATION.md](6WB_PALETTE_CALIBRATION.md) for the
20-line ordering, monotonic ten-second deadline and source disable switch.

The published JayJay reference describes **both 1F/20 as a perspective A/B pair**,
and groups 21/22 with perspective-sized surfaces; it does not establish a flat
central pair on this F31. The hypothesis "1F perspective /20 flat" is NOT
established by their being two buffers. This RC requires distinct A/B pairs
for its atomic swaps; it does not silently reinterpret one buffer of a pair as
a different plane. Dimensions stay 221x158 for both configured central pairs.
C04 validates configured flat handles too (10 checks instead of the former 8).
ACK/dimension agreement is not proof of visible flat rendering.

Consequently the flat pair must be supplied from actual vehicle evidence before
an end-to-end flat/roundabout test can succeed. There is no default guessed 21/22
assignment. This is an unresolved hardware mapping, not an assertion that the
release candidate is already operational on the car.

The kit's swatch helper remains available, and the startup presentation now
shows 20 native lines through the existing display worker. No separate test
program, IPC service or startup executable is installed. The provisional colour
assignments come from the user, not RGB estimates from photographs.
RGB remains usable for the existing RoadIcon bitmap encoding; its pixels use
kit styling and the existing ABGR/RLE32 codec and 4096-byte cap.

## Tests and remaining validation

Executed without compilation:

* 8 Python source/data tests: all 6612 imported coordinates and 585 styles,
  explicit enum mapping, no exit-example selection, angular mirror convention,
  uniform fits against the supplied geometry at 180x77, 220x157 and 282x39,
  per-direction lane preservation, shared bar condition, no legacy draw calls,
  build lists, C++98 lexical subset, delimiters, command limits and atomic append.
* 2 Python navigation-log reader tests.
* Git whitespace/diff review; no new binaries, logs or credentials in the change.

These are source/data checks. The independent Python maths checks do NOT execute
C++. `tests/test_hud6wb_kit.cpp` prepares deterministic runtime tests for angles
1,15,90,127,180,225,270,359,360 in both directions, generic rotatories, 2..6 lanes,
per-branch highlighting, bounds, palette zero/unmapped cases and bar/plane rules.
Existing renderer tests use explicitly synthetic colour indices, never car
measurements. No C++ tests were compiled or executed, and no QNX compiler/SDK
was searched or configured.

Still required on Windows/F31:

1. Compile and run appropriate C++ checks on the user's environment; verify the
   resulting ARM ELF separately. Compilation and ABI compatibility are unproven.
2. Observe the provisional colour roles and identify the flat A/B pair. Verify
   actual C04 dimensions and each pair's visibility; do not infer visibility from ACK.
3. Validate native joins, subpixel stroke quantization, graphite/gold contrast,
   optional black checker cells and Comfort/Sport/Eco presentation.
4. Validate real exit-angle convention and joins for clockwise/counter-clockwise
   rotatories, intermediate angles, missing angles and exit numbers.
5. Validate 2..6 lanes, mixed highlights, obliques/sharp turns, U-turns and unknown
   positions using the actual decoded data. Only Google data was captured in
   the available JSONL; Waze operation is not claimed as hardware-tested.
6. Check chunked update ordering, plane transitions using the exact bar visibility,
   ending navigation/reconnecting and absence of a stale/doubled picture.
7. Define a measured perspective compensation if a custom 3D representation is
   required beyond the terminal's native projection. It is not supplied by the kit.

No new executable, installation, automatic startup, GitHub release or positive
vehicle test is produced by this source candidate.
