# 6WB flat renderer and dual calibration (experimental F31)

Base: `1574be1c311e8058d1de06d8ed28fe741a022104`. No QNX/host compilation,
installation or release is performed by this change.

## Coordinate contract and scope

The centralized `Bmw6wbProfile::base.canvas/base.glyph` default remains
(0,0)..(232,205), i.e. a 233x206 vector input. The renderer receives that
rectangle and fits the entire composition using one `min(width/bboxWidth,
height/bboxHeight)` scale, including the outline. No perspective, horizon
threshold, shear or independent X/Y scaling is applied in software. The
physical transform and visible bounds of 1F/20 on the F31 remain unconfirmed.
The historical JayJay y~70 observation is NOT a visibility constraint here.
User arrow-area overrides remain centralized in the existing 6WB configuration.

The main pair remains 1F/20, and existing eight C04 checks still validate
measured dimensions (main C04 is 221x158, distinct from vector input). Decoder,
NAV model, MOST/DSI, timeout/correlation, asynchronous scheduler, manual start,
Settings, 6WA/J29 and persistent diagnostic logging are unchanged.

## Ordered calibration lifecycle

Once per new NAV route, after the existing resource check and pending-picture
completion:

1. A: palette (unchanged sample coordinates below) + upper bitmap 1; hold 10 s.
2. B: stepped coordinate rulers + upper bitmap 2; hold 10 s.
3. Ordered clear/hide of the visible central and upper buffers, then render
   the latest navigation snapshot with a fresh normal-render state.

Each hold begins when the existing worker observes local dispatch completion,
using CLOCK_MONOTONIC. The second picture is queued as soon as A's deadline is
observed; there is no extra deliberate pause, sleep or NAV frame between them.
Dispatch and worker scheduling add finite transition time: 10+10 s software
holds are not a claim of exactly 20.000 s of measured panel light. The two
surfaces are ordered MOST commands, not an atomic multi-surface hardware flip.

Duplicate ACTIVE/reroute do not restart either test. A navigation end, arrows
off, session end or link/failure may cancel, as before. A failed/unverified
terminal is not reset or forcibly written to for cleanup. No resend/retry is
introduced. `ENABLE_PALETTE_CALIBRATION=false` disables the complete sequence.

## A: unchanged twenty sample rectangles

Each rectangle is selected with 21B index=row*4+column, then 21D type 03,
four corners. X columns: 25..64, 73..112, 121..160, 169..208.
Y rows: 0..17, 37..54, 74..91, 111..128, 148..165. Samples never overlap.
There is no vertical offset. All twenty values 0..19 occur once, regardless
of apparent transparency. Reference geometry is unchanged from 1574be1:

* Vertical bars: x=21..23, 69..71, 117..119, 165..167, 210..212; y=0..169.
* Horizontal bars: y=19..21, 56..58, 93..95, 130..132, 167..169; x=21..212.

Reference bars now select **6**, the newly requested expected-white index,
instead of the previous 2 (now expected-grey). They do not cover sample fills.
The top stays open so samples start at y=0. Reference IDs 40..49 hex,
sample IDs 80..93 hex. No 218/219 is used.

## B: exact stepped rulers

There are 41 steps per side, each 5 units high, thickness 2. Left selects
index 6 (expected white), right index 5 (expected red). For step `i=0..40`:

```
y0=5*i; y1=y0+5
a=5*min(i,40-i)
j=min(i+1,40); z=5*min(j,40-j)
```

Left risers start at x=a; the tread moves toward x=z at y1. For z>=a the
filled polygon is `(a,y0),(a+2,y0),(a+2,y1-2),(z+2,y1-2),(z+2,y1),(a,y1)`.
For z<a it is `(a,y0),(a+2,y0),(a+2,y1),(z,y1),(z,y1-2),(a,y1-2)`.
For a=z (last step), use the four rectangle corners spanning x=a..a+2,
y=y0..y1. The right polygon mirrors EVERY vertex with `xRight=232-xLeft`, then reverses
vertex order to retain the same winding as the left filled polygons.
Thus this table plus the formula specifies every vertex, not just a bounding box.
All polygons use 21D type 03; no unverified line colour selector is involved.

| i | y0..y1 | a → z (left) | 232-a → 232-z (right) |
|---:|---|---|---|
| 0 | 0..5 | 0 → 5 | 232 → 227 |
| 1 | 5..10 | 5 → 10 | 227 → 222 |
| 2 | 10..15 | 10 → 15 | 222 → 217 |
| 3 | 15..20 | 15 → 20 | 217 → 212 |
| 4 | 20..25 | 20 → 25 | 212 → 207 |
| 5 | 25..30 | 25 → 30 | 207 → 202 |
| 6 | 30..35 | 30 → 35 | 202 → 197 |
| 7 | 35..40 | 35 → 40 | 197 → 192 |
| 8 | 40..45 | 40 → 45 | 192 → 187 |
| 9 | 45..50 | 45 → 50 | 187 → 182 |
| 10 | 50..55 | 50 → 55 | 182 → 177 |
| 11 | 55..60 | 55 → 60 | 177 → 172 |
| 12 | 60..65 | 60 → 65 | 172 → 167 |
| 13 | 65..70 | 65 → 70 | 167 → 162 |
| 14 | 70..75 | 70 → 75 | 162 → 157 |
| 15 | 75..80 | 75 → 80 | 157 → 152 |
| 16 | 80..85 | 80 → 85 | 152 → 147 |
| 17 | 85..90 | 85 → 90 | 147 → 142 |
| 18 | 90..95 | 90 → 95 | 142 → 137 |
| 19 | 95..100 | 95 → 100 | 137 → 132 |
| 20 | 100..105 | 100 → 95 | 132 → 137 |
| 21 | 105..110 | 95 → 90 | 137 → 142 |
| 22 | 110..115 | 90 → 85 | 142 → 147 |
| 23 | 115..120 | 85 → 80 | 147 → 152 |
| 24 | 120..125 | 80 → 75 | 152 → 157 |
| 25 | 125..130 | 75 → 70 | 157 → 162 |
| 26 | 130..135 | 70 → 65 | 162 → 167 |
| 27 | 135..140 | 65 → 60 | 167 → 172 |
| 28 | 140..145 | 60 → 55 | 172 → 177 |
| 29 | 145..150 | 55 → 50 | 177 → 182 |
| 30 | 150..155 | 50 → 45 | 182 → 187 |
| 31 | 155..160 | 45 → 40 | 187 → 192 |
| 32 | 160..165 | 40 → 35 | 192 → 197 |
| 33 | 165..170 | 35 → 30 | 197 → 202 |
| 34 | 170..175 | 30 → 25 | 202 → 207 |
| 35 | 175..180 | 25 → 20 | 207 → 212 |
| 36 | 180..185 | 20 → 15 | 212 → 217 |
| 37 | 185..190 | 15 → 10 | 217 → 222 |
| 38 | 190..195 | 10 → 5 | 222 → 227 |
| 39 | 195..200 | 5 → 0 | 227 → 232 |
| 40 | 200..205 | 0 → 0 | 232 → 232 |

Left polygon IDs are 0x100+i; right IDs 0x140+i. Shapes in adjacent bands
only meet at boundaries; the two series stay separated (left max x=102,
right min x=130). Top y=0, bottom y=205, and external x=0/232 are actually
encoded, without inset or clamp.

Right-only detached 3x2 coordinate-unit ticks make the series distinguishable
even if palette colours are unexpected. For i=0,5,...40, tick bounds are
`(232-a-6,5*i+1)..(232-a-3,5*i+3)`; IDs 0x200+i:

| i | Exact tick bounds |
|---:|---|
| 0 | (226,1)..(229,3) |
| 5 | (201,26)..(204,28) |
| 10 | (176,51)..(179,53) |
| 15 | (151,76)..(154,78) |
| 20 | (126,101)..(129,103) |
| 25 | (151,126)..(154,128) |
| 30 | (176,151)..(179,153) |
| 35 | (201,176)..(204,178) |
| 40 | (226,201)..(229,203) |

Ticks do not overlap the stair fills. All vertices are within configured
bounds; full visibility, clipping and fading remain the purpose of the trial.

## Upper bitmap tests

The project and JayJay source identify 18/19 as alternate RoadIcon buffers,
305x44 by C04. We reuse the existing inactive-buffer load and 20E visibility
switch; we do not treat them as simultaneous independent display areas.
Physical F31 placement of the full image remains to be checked.

Pixels are generated in RAM, without image files, font libraries, external I/O,
alpha blending or antialiasing. Both images are exactly 305x44. A 1px perimeter,
four 5x5 corner markers (x=3..7/297..301, y=3..7/36..40) and a centered 5x7
bitmap font at integer scale 2 are encoded. Text is 14 pixels high, x=75..228,
y=15..28; the cell width includes whitespace between glyphs.

| Image | Text | Background ARGB | Foreground ARGB |
|---|---|---|---|
| 1 | BITMAP TEST 1 | FFFFFF00 (yellow) | FF00FF00 (green) |
| 2 | BITMAP TEST 2 | FFFF0000 (red) | FF00FFFF (cyan) |

The existing `bitmap()` emits `[handle:u16][x=0:u16][y=0:u16][01][01][00][id=0]`
followed by **NBT_RLE32**, NOT a BMP/PNG. Each colour is serialized ABGR:
A,B,G,R. Run tags have high bit=repeat, 7-bit count <=127; literal runs have
count followed by pixels. The existing compressor pads to the next 8-byte
boundary, including 8 bytes if already aligned. Nominal dimensions come from
the verified surface; no new width/height fields are invented in C05.

Independent Python encoding of the production glyph data gives total C05
payload sizes **2882 bytes** and **2898 bytes**, including 10-byte envelope.
Both remain below the existing 4096-byte single-segment cap. Production code
checks this cap and refuses an oversize image atomically, without truncation.
C05 retains txType=0, the established project path; no new segmentation,
transport variant, Ack wait, retry or protocol change is introduced.

Reference: JayJay3071/nbt-android-auto commit
12c8411618acd3eea5fb0a5ff1a57ef1c87761e0, nbt_cluster.cpp:42,349–398,490–498;
docs/CLUSTER-HUDTERMINAL.md:169–190. Later JayJay experiments discuss tx1 for
larger images; this change stays with the existing working tx0 under 4 KiB.

## Flat maneuver composition and palette

| Role | Semantic source field | HUD index | Expected colour only |
|---|---|---:|---|
| Road/background | Graphite, Black | 3 | dark grey |
| Road edges / unselected lane details | Gold, Gray | 2 | grey |
| Guidance and symbols | White | 6 | white |
| Guidance/symbol outline | Red | 5 | red |

No definitive colour or RGB palette is asserted for the cluster. Existing
semantic names retain source compatibility, but no gold index is assumed.
Small RGB next-maneuver bitmaps use corresponding provisional RGB styling;
calibration bitmaps are independent explicit RGB tests as specified above.

Road families 1..29 and 36 use new flat compositions, not a stretch of imported
artwork. Straight roads are 620 design units wide; turning roads 360; U-turn
roads 240 with a 250-unit bend radius. Thin road borders are 6 design units.
90/135-degree turns use sampled tangent bends (8/12 segments) to avoid the
self-intersection of a wide sharp-corner silhouette. Keep/fork/ramp/exit and
merge families include full-width branch roads. AA still selects the actual
maneuver; no extra recommended direction is invented.

Each selected arrow is one CLOSED filled silhouette, including shaft and head:
white 6 fill plus red 5 perimeter. Half-width is 60 design units (45 for U-turn),
head half-width 2.3*half, head length 2.5*half; 6-unit outline. The end of the
body is shortened by the head length so its tip reaches the road endpoint.
No thick native line approximates the shaft. Left/right share mirrored designs.
Lane composites retain their per-direction recommendation data and use white/
red for selected branches; they are not replaced with smaller single arrows.

Roundabouts use concentric 360/190-unit circles at (500,475), a 275-unit
guidance centre line and a full-composition uniform fit. The ring's sampled
vertices remain circular before/after uniform fitting; final integer polygon
rasterization inevitably has pixel quantization. Entry/exit tails and numbers
count toward the complete bounding box. Missing angle never invents an exit.
Ferry/train and arrival categories retain their distinct supplied silhouettes
and original proportions, with thin red outlines and the unified palette;
they are not replaced by road-arrow geometry. Coverage 1..46 is retained;
0 remains hidden/unknown, and unsupported extra AA types are not invented.

The encoder expands ALL strokes into bounded filled polygon strips and round
joins/caps. Before each fill/outline group it explicitly selects 21B. It emits
no 218, 219, 21C, native thick line or new protocol primitive for this rendering.
The existing even-odd hole decomposition and 99-point polygon limit remain.
Nondegenerate filled polygons are normalized to consistent winding; coincident
arc/quadratic endpoints are deduplicated before silhouette normals are computed.

## Bounded work and validation

* A calibration batch: **64 calls** (including bitmap load/swap).
* B calibration batch: **106 calls** (82 step polygons, nine ticks, two colours,
  clear/hide/switch and bitmap commands included).
* Final calibration clear: eight calls, both buffers of both pairs clear/hide.
* Normal pictures retain existing ordered chunks of at most **128 calls**.
  An unsubmitted picture is bounded at 4096 calls, individual kit encoding at
  2048. Visibility flips remain after the geometry, not between its chunks.
* Image generation/compression and geometry run in the existing cluster worker,
  not the AA receiver or DSI callback; bounded input/payload, no added threads,
  blocking sleep, file I/O or synchronous Ack wait.

39 Python source/data/numerical checks cover source contracts, all stored reference coordinates/styles,
step geometry/bounds/counts, tick separation, exact palette-cell coordinates,
bitmap pixel colours/dimensions/RLE round-trip/padding/cap, uniform fit, circle
math, road left/right symmetry, non-self-intersecting road/arrow silhouettes,
source coverage and isolation of NAV/transport. Independent numerical road
previews were inspected; these do NOT execute C++ or emulate the real cluster.
C++ tests are updated for later Windows/QNX execution, but not compiled/run.
Actual QNX compilation and all 46 emitted maneuver sequences must still be
verified locally. No claim of runtime/hardware regression-free operation is
made from static/numerical checks alone.

F31 checks: both full charts including y=0/205 and x=0/232; apparent step/tick
positions, fading, all palette values, two full 305x44 bitmaps and corner marks;
10+10 holds and latest-NAV resumption; stop/restart/reroute/arrow cancellation;
all maneuver families, red outline continuity and circular roundabout display.
A native cluster transform cannot be removed or certified by software-only
uniform fitting. No visible limit or physical colour is confirmed here.

## Changed files

### Production (10 files)

* `milestone-7/src/cluster/grafite_oro/artwork6wb.cpp`
* `milestone-7/src/cluster/grafite_oro/artwork6wb.hpp`
* `milestone-7/src/cluster/grafite_oro/hudterminal_palette.cpp`
* `milestone-7/src/cluster/grafite_oro/hudterminal_palette.hpp`
* `milestone-7/src/cluster/hud6wb_calibration.hpp`
* `milestone-7/src/cluster/hud6wb_codec.cpp`
* `milestone-7/src/cluster/hud6wb_codec.hpp`
* `milestone-7/src/cluster/hud6wb_display.cpp`
* `milestone-7/src/cluster/hud6wb_display.hpp`
* `milestone-7/src/cluster/hud6wb_kit.cpp`

### Tests (9 files)

* `milestone-7/tests/hud6wb_kit_test_util.hpp`
* `milestone-7/tests/test_hud6wb_calibration.cpp`
* `milestone-7/tests/test_hud6wb_calibration_static.py`
* `milestone-7/tests/test_hud6wb_dual_static.py`
* `milestone-7/tests/test_hud6wb_flat_geometry.py`
* `milestone-7/tests/test_hud6wb_geometry_static.py`
* `milestone-7/tests/test_hud6wb_kit.cpp`
* `milestone-7/tests/test_hud6wb_kit_static.py`
* `milestone-7/tests/test_hud6wb_surface_static.py`

### Documentation (4 files)

* `milestone-7/docs/6WB_DUAL_CALIBRATION_FLAT.md`
* `milestone-7/docs/6WB_GRAFITE_ORO_RC.md`
* `milestone-7/docs/6WB_PALETTE_CALIBRATION.md`
* `milestone-7/docs/6WB_VECTOR_COORDINATES.md`
