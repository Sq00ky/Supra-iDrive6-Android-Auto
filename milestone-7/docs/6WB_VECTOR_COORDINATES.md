> Coordinate history and palette-sample mapping. Current two-stage operation,
> expected-white reference index 6, bitmap tests and 64/106-call budgets are
> documented in [6WB_DUAL_CALIBRATION_FLAT.md](6WB_DUAL_CALIBRATION_FLAT.md).

# Experimental 6WB vector coordinates and identifiable palette grid

## Evidence and distinct dimensions

Reference inspected at JayJay3071/nbt-android-auto commit
`12c8411618acd3eea5fb0a5ff1a57ef1c87761e0`:

* [`nbt_cluster.cpp`, lines 9–10](https://github.com/JayJay3071/nbt-android-auto/blob/12c8411618acd3eea5fb0a5ff1a57ef1c87761e0/src/android_auto/knob/nbt_cluster.cpp#L9-L10)
  describes the 1F vector plane as **about 233 x 206**, visible from y~70.
* [HUDTerminal notes, lines 64–69](https://github.com/JayJay3071/nbt-android-auto/blob/12c8411618acd3eea5fb0a5ff1a57ef1c87761e0/docs/CLUSTER-HUDTERMINAL.md#L64-L69)
  report the 21B colour/style selector and an approximate perspective horizon
  on that reference vehicle. This is not a verified F31 visibility boundary.
  Indices 0..19 remain tested without assigning final RGB roles from a photograph.
* [ImageGetSize table, lines 156–166](https://github.com/JayJay3071/nbt-android-auto/blob/12c8411618acd3eea5fb0a5ff1a57ef1c87761e0/docs/CLUSTER-HUDTERMINAL.md#L156-L166)
  instead reports **221 x 158** for 1F/20. `nbt_cluster.cpp` lines 664–666 also
  use 221x158 for bitmaps matching ImageGetSize.
* The same notes warn that lines directly on an area boundary may be clipped.
  They do not provide a measured F31 homography or proof that every coordinate
  within the approximate 233x206 range is visible on this vehicle.

The experiment deliberately separates these facts instead of fabricating a new
C04 response or weakening validation:

| Profile field | Current meaning / value |
|---|---|
| `main` | Vector input extent: handles 1F/20, **233 x 206** |
| `mainImageWidth/mainImageHeight` | Expected C04 measurement: **221 x 158**, unchanged |
| `base.canvas` | Full inclusive coordinate bounds **(0,0)..(232,205)** |
| `base.glyph` | Same full bounds by default; old (20,74)..(200,151) box removed |
| `kPerspectiveHorizon` | Historical reference **70**; NOT used by the calibration chart |

Only the C04 expectation fields are used for size-cache verification. The
procedure still validates the same eight handles and keeps host/tag/function/
handle matching and finite timeouts intact. No new handle, instance allocation,
MOST packet format or transport channel is introduced.

## Main maneuver fit

All 46 supported visible maneuver families remain on the same 1F/20 A/B pair.
The existing kit generator now receives the full canvas with **margin=0**.
It scales uniformly using the actual geometry including stroke widths. Aspect
ratio/painter order and semantic geometries are unchanged. There is no new
source-side crop at y=70 for maneuvers; any invisible/cut portion will be observed
on the vehicle, as requested. We do not copy JayJay's separate kLift/kStretch
arrow transformation because that would change the kit geometry/proportions.

The shared HudProfile type does not impose the 6WA's canvas. The 6WB-specific
validation and optional NBT_6WB_ARROW_* settings now use 233x206 as their limit.
Explicit user overrides are still honoured; calibration uses the full canvas
independently of those arrow overrides. No final palette assignment changes:
Road=3, road edge=2, guidance=6, outline=5 provisional.

## Palette reference: 4 columns x 5 rows

The F31 trial of the previous chart showed only the first three sample rows
and fading near the lateral extremes. It did not establish a usable horizon.
The new coordinate experiment deliberately starts the first samples at **Y=0**,
with no vertical offset, and narrows the columns to the user-requested bounds.
The full vector profile and all maneuver geometries remain unchanged.

Five vertical and five horizontal **filled white reference bars** identify the
cells. They use **21B index 6** and 21D type 03, without 218. Index 6 is the user-selected expected-white reference,
not an inferred definitive RGB value.
The top of the grid is open: a top bar at Y=0 would overlap the required first
sample row. Every cell has white side references and a bottom reference. These
references are separate from the samples, with no white backing: a transparent
fill exposes the background instead of becoming indistinguishable from white.
Reference bars may cross each other; samples overlap neither bars nor samples.

Sample IDs are `0x80..0x93`, reference IDs `0x40..0x49`. Sample indices are decimal
0..19 exactly once, left to right, top to bottom. One additional index-6 selection
colours all reference bars. Indices 0, 1 and 10 are not assumed transparent.

### Exact sample bounds (input coordinates)

Each entry is `(x0,y0)..(x1,y1)`. Four clockwise corners are encoded as
`(x0,y0), (x1,y0), (x1,y1), (x0,y1)`; closure is implicit, type 03/count 04.

| Row | Column 1 | Column 2 | Column 3 | Column 4 |
|---|---|---|---|---|
| 1 | **0:** (25,0)..(64,17) | **1:** (73,0)..(112,17) | **2:** (121,0)..(160,17) | **3:** (169,0)..(208,17) |
| 2 | **4:** (25,37)..(64,54) | **5:** (73,37)..(112,54) | **6:** (121,37)..(160,54) | **7:** (169,37)..(208,54) |
| 3 | **8:** (25,74)..(64,91) | **9:** (73,74)..(112,91) | **10:** (121,74)..(160,91) | **11:** (169,74)..(208,91) |
| 4 | **12:** (25,111)..(64,128) | **13:** (73,111)..(112,128) | **14:** (121,111)..(160,128) | **15:** (169,111)..(208,128) |
| 5 | **16:** (25,148)..(64,165) | **17:** (73,148)..(112,165) | **18:** (121,148)..(160,165) | **19:** (169,148)..(208,165) |

Each sample spans **39 x 17 coordinate units** (40 x 18 inclusive coordinates).
Column pitch is 48 and row pitch is 37. The complete sample envelope is
**(25,0)..(208,165)**, 184 coordinates wide. No offset, horizon clamp, arrow
layout override or perspective compensation is applied by calibration code.

### Exact reference bars

* Vertical: x=**21..23**, **69..71**, **117..119**, **165..167**, **210..212**;
  all span y=0..169.
* Horizontal (below each sample row): y=**19..21**, **56..58**, **93..95**,
  **130..132**, **167..169**; all span x=21..212.

All sample/reference coordinates lie within (0,0)..(232,205). The full reference
envelope is (21,0)..(212,169), with gaps separating every sample from the bars.
The palette phase now totals **64 calls**, including the upper bitmap test:
1 clear, 1 reference colour, 10 reference polygons, 40 sample calls, 6 hidden
elements, 1 central A/B switch, 1 container visibility and 4 bitmap calls. Four vertices per polygon remain below the 99-point
limit; the batch remains below the 128-call implementation budget.

The existing C04 gate, once-per-navigation lifecycle, local-dispatch-based
10-second monotonic deadline, clearing and latest-navigation resumption are
unchanged. No extra application, retry, blocking delay or calibration phase is
introduced.

## Verification limits and next F31 observation

Executed without compilation: **24 Python source/data checks** (4 expanded
geometry, 7 calibration, 5 single-surface, 8 kit) and lexical/whitespace review.
They cover the complete grid, no sample/sample or sample/reference overlap,
full 233x206 bounds, zero central margin, independent measured C04 dimensions,
reference/sample IDs and unchanged timer/colours/transport/NAV/diagnostics.
The calibration isolation check now compares against `1e4d531`: only the chart
body may differ in production sources.
Independent fit maths cover zero and two-unit margins, including 232x205 spans.
These checks do not execute C++ or simulate the actual display transform.

C++ tests are updated for the full range, both buffers and C04 responses still
221x158, with assertions that emitted points extend beyond the old 220/157
bounds. They are prepared, NOT compiled or run. No SDK was searched/configured.

The coordinate range is experimental on the F31; no horizon assumption restricts
the new chart. Starting at Y=0 tests visibility rather than proving it.
We cannot certify that all 20 samples are physically visible before the next
vehicle test. If reference frames are missing too, the observation indicates
geometry/clipping/reference visibility, not proof of a transparent index. If a
frame is visible but its inner fill matches the background, transparency or
low contrast becomes a testable explanation. Capture the whole framed chart
and note the driving/display mode before assigning permanent colour roles.

No compile, installation or GitHub release was performed by this change.
