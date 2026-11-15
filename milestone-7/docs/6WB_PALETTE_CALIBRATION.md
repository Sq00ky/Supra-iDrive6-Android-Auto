# Built-in 6WB palette startup presentation

This change uses the existing 6WB display, C04 cache, transport and worker. It
does not install a separate diagnostic application, service, IPC or new thread.

## Sequence

1. A real navigation start (INACTIVE -> ACTIVE, or first ACTIVE) arms one chart.
   A generation counter in the existing NAV callback preserves rapid stop/start
   transitions between worker snapshots; it does not change stored NAV bodies.
   Duplicate ACTIVE and REROUTING do not restart the chart. An AA session end
   resets the lifecycle. Turning arrows off cancels it without restarting it on
   the same route.
2. Existing surface validation must succeed, and already accepted drawing chunks
   finish first. No chart is sent before `BmwResources::advance()` verifies the
   configured surfaces. Existing valid C04 results remain cached as before.
3. The inactive buffer in the verified 1F/20 pair is cleared. It receives a
   **4-column x 5-row grid of 20 filled rectangles**, one per index 0..19.
   For each cell the commands are adjacent: `21B [handle:u16][index:u8]`, then
   `21D [handle:u16][item:u16][03][04][four x/y pairs]`. There is no 218
   (SetLineColor), polyline, or new line-width setting in this chart.
   The complete **51-command** batch is within the existing 128-call limit.
4. Other navigation elements are hidden, then the finished chart is exposed by
   the existing A/B visibility switch. The clock is armed only after local
   dispatch completes; asynchronous Ack latency does not start another wait.
5. Deadline = observed local dispatch completion + exactly 10.0 monotonic
   seconds. NAV messages continue through the normal decoder/storage callback;
   they cannot replace the chart or restart its clock. The worker still advances
   the countdown during REROUTING/no current NavView.
6. At the deadline, the chart is cleared/hidden. Once that short ordered clear
   is locally dispatched, the same tick passes the latest current NavView into
   Core. No stale maneuver from the start of calibration is replayed and there
   is no additional ten-second delay. Normal geometry delivery still takes its
   normal finite transport dispatch time.

An explicit navigation end, arrow setting off, daemon shutdown or link failure
can cancel the presentation. Those are cancellation/lifecycle events, not an
interruption by incoming navigation instructions. No retry/reset of a failed
terminal is introduced. A link change during an already displayed chart aborts
that attempt; it is not resent within the same navigation. Initial discovery
while a chart is still pending does not consume its one attempt.

## Grid reference for photographs

Indices below are **decimal**, increasing left to right, then top to bottom.
The corresponding colour bytes range from 0x00 to 0x13, not 0x00 to 0x19.
Do not renumber a cell if its fill is invisible/transparent or looks identical
to an adjacent cell. No colour identity is inferred in advance.

| Row (top to bottom) | Column 1 (left) | Column 2 | Column 3 | Column 4 (right) |
|---|---:|---:|---:|---:|
| 1 | 0 | 1 | 2 | 3 |
| 2 | 4 | 5 | 6 | 7 |
| 3 | 8 | 9 | 10 | 11 |
| 4 | 12 | 13 | 14 | 15 |
| 5 | 16 | 17 | 18 | 19 |

Input coordinates before the terminal's perspective transform:

* Column x extents: **26..62**, **70..106**, **114..150**, **158..194**.
* Row y extents: **75..85**, **91..101**, **107..117**, **123..133**, **139..149**.
* Each rectangle spans 36 x 10 coordinate units, with four clockwise vertices
  (top-left, top-right, bottom-right, bottom-left). Closure is implicit in the
  filled polygon. Eight units separate columns; six separate rows.
* The full chart stays below the known ~70px horizon, inside the existing
  `(20,74)..(200,151)` glyph input area of the 221x158 central surface. Physical
  displayed dimensions and gaps are transformed by the cluster.

The first real F31 trial displayed all previous 218-coloured lines as white.
This test isolates the 21B fill selector instead. The normal maneuver renderer,
including its current 218 contour handling and provisional 2/14/3 roles, is
unchanged. This source change does not claim the fill colours have been observed
on the F31 yet.

## Timing precision

The source implements a **10,000 ms software deadline**, not a physical-display
measurement. The existing worker can wake on that deadline, with at most 50 ms
sleep slices for shutdown responsiveness; it polls local send completion every
10 ms without blocking the NAV receiver. An overdue deadline cannot busy-spin.

QNX scheduling, the gap between actual last dispatch and its observation, MOST
transmission, command execution and actual panel visibility are not timestamped
as hardware presentation events. Therefore exact 10.000 seconds of visible
light on the panel cannot be guaranteed or claimed without a vehicle measurement.
There is no blocking sleep(10) and no timeout increase.

## Colour roles

In `src/cluster/hud6wb_kit.cpp`:

```
static const uint8_t COLOR_CONTOUR = 2;
static const uint8_t COLOR_INFILL = 14;
static const uint8_t COLOR_ACTIVE_SELECTION = 3;
```

Kit Gold (edges) -> Contour; Graphite, Gray and Black -> Infill; kit White
(guidance arrows and selected infill) -> ActiveSelection.
The geometries and painter order are unchanged. Index 3 is deliberately
provisional: the code does not assume its actual displayed colour. Index 14 is used as requested,
including if the car's chart reveals a different/transparent appearance.
`GrafiteKit` and the display no longer read `6wb-kit.palette` at all. Calibration
and every central maneuver use only 0x1F/0x20 with the existing A/B buffering and
verified input coordinates. No separate central pair is configured or required.
The existing eight C04 checks remain; colours and normal rendering work without
the file.

## Disable

Set `ENABLE_PALETTE_CALIBRATION = false` in
`src/cluster/hud6wb_calibration.hpp` before the user's next local compilation.
Colours still use the three compiled indices. No env entry or new application
is required. SHADOW follows the same lifecycle and logs commands without sending.

## Checks

Executed in Codex for this change: seven calibration source/data tests, five
single-surface contracts and eight kit source/data checks (20 checks total),
plus whitespace/static review. An isolation check compares all production code
outside the chart function to `f5dea737f537371564c6034a9a7f17f13a947196`. No C++ or QNX
compilation/execution. `test_hud6wb_calibration.cpp` is prepared for subsequent
runtime checking of the exact boundary, duplicate ACTIVE/REROUTING, route restart,
disabled mode, both A/B buffers, all 20 fill/polygon payload pairs, four vertices
per cell, grid indices, bounds, exact 51-call budget and provisional roles.

Remaining F31 checks: rectangle visibility/colour/order, meaning of all 20 indices,
visible hold duration, normal guidance resumption with updated maneuvers, and
start/stop/reroute behavior. No binary was built or installed by this change.
