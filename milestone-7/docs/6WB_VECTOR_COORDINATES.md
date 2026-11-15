# Experimental 6WB vector coordinates and identifiable palette grid

## Evidence and distinct dimensions

Reference inspected at JayJay3071/nbt-android-auto commit
`12c8411618acd3eea5fb0a5ff1a57ef1c87761e0`:

* [`nbt_cluster.cpp`, lines 9–10](https://github.com/JayJay3071/nbt-android-auto/blob/12c8411618acd3eea5fb0a5ff1a57ef1c87761e0/src/android_auto/knob/nbt_cluster.cpp#L9-L10)
  describes the 1F vector plane as **about 233 x 206**, visible from y~70.
* [HUDTerminal notes, lines 64–69](https://github.com/JayJay3071/nbt-android-auto/blob/12c8411618acd3eea5fb0a5ff1a57ef1c87761e0/docs/CLUSTER-HUDTERMINAL.md#L64-L69)
  confirm the 21B colour/style selector, perspective and the invisible region
  above the approximate horizon. Indices 0..19 remain tested without assigning
  final RGB roles from a photograph.
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
| `kPerspectiveHorizon` | **70**, an approximate experimental boundary from JayJay |

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
Contour=2, Infill=14, ActiveSelection=3 provisional.

## Palette reference: 4 columns x 5 rows

The chart uses the below-horizon portion **x=0..232, y=70..205**. Five vertical
and six horizontal **filled white reference bars** form a shared grid. They
use **21B index 2** and 21D type 03, not the unreliable 218 outline selector.
White 2 is the existing contour/reference choice and is also labelled white in
JayJay's source (line 46); its visibility in each F31 mode is still an on-car
observation, not a new measured RGB calibration.

The 20 test fills sit inside those frames with a clear background gap. There
is NO white backing under a sample: transparent samples reveal the background,
whereas a white sample has a white interior separated from its frame by a gap.
The reference stays visible independently of the tested fill. Global frame-bar
intersections overlap in the same white colour; test samples overlap neither
each other nor the reference bars.

Test sample IDs are `0x80..0x93`. Reference IDs are `0x40..0x4A`. Sample indices
are decimal 0..19 exactly once, left to right and top to bottom; there is one
additional index-2 selection for the entire reference grid. None of 0, 1 or 10
is assumed transparent or omitted from the test.

### Exact sample bounds (input coordinates)

Each entry is `(x0,y0)..(x1,y1)`. Four clockwise corners are encoded as
`(x0,y0), (x1,y0), (x1,y1), (x0,y1)`; closure is implicit, type 03/count 04.

| Row | Column 1 | Column 2 | Column 3 | Column 4 |
|---|---|---|---|---|
| 1 | **0:** (5,75)..(53,92) | **1:** (63,75)..(111,92) | **2:** (121,75)..(169,92) | **3:** (179,75)..(227,92) |
| 2 | **4:** (5,102)..(53,119) | **5:** (63,102)..(111,119) | **6:** (121,102)..(169,119) | **7:** (179,102)..(227,119) |
| 3 | **8:** (5,129)..(53,146) | **9:** (63,129)..(111,146) | **10:** (121,129)..(169,146) | **11:** (179,129)..(227,146) |
| 4 | **12:** (5,156)..(53,173) | **13:** (63,156)..(111,173) | **14:** (121,156)..(169,173) | **15:** (179,156)..(227,173) |
| 5 | **16:** (5,183)..(53,200) | **17:** (63,183)..(111,200) | **18:** (121,183)..(169,200) | **19:** (179,183)..(227,200) |

Each sample spans **48 x 17 coordinate units**. The 58-unit horizontal and
27-unit vertical pitches leave ten units between adjacent test fills. The
five-unit cell inset exists to separate a fill from its two-unit reference
border, not to shrink the overall vector surface. Final displayed pixel sizes
and apparent spacing depend on the cluster's perspective transform.

### Exact reference bars

* Vertical: x=**0..2**, **58..60**, **116..118**, **174..176**, **230..232**;
  all span y=70..205.
* Horizontal: y=**70..72**, **97..99**, **124..126**, **151..153**,
  **178..180**, **203..205**; all span x=0..232.

The total is **63 calls**: 1 clear, 1 reference colour, 11 reference polygons,
20*(1 tested colour + 1 sample polygon), 8 hidden elements, 1 A/B switch and
1 container visibility. Every polygon has four points, well below the real
99-point limit. The batch is below the existing 128-call pipeline budget.

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
Independent fit maths cover zero and two-unit margins, including 232x205 spans.
These checks do not execute C++ or simulate the actual display transform.

C++ tests are updated for the full range, both buffers and C04 responses still
221x158, with assertions that emitted points extend beyond the old 220/157
bounds. They are prepared, NOT compiled or run. No SDK was searched/configured.

The approximate horizon and coordinate range are experimental on the F31.
We cannot certify that all 20 samples are physically visible before the next
vehicle test. If reference frames are missing too, the observation indicates
geometry/clipping/reference visibility, not proof of a transparent index. If a
frame is visible but its inner fill matches the background, transparency or
low contrast becomes a testable explanation. Capture the whole framed chart
and note the driving/display mode before assigning permanent colour roles.

No compile, installation or GitHub release was performed by this change.
