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
3. The inactive buffer in the verified 1F/20 pair is cleared. It receives 20
   horizontal native polylines, width 2, indices 0..19 in increasing vertical
   order. Coordinates are x=22..198, y=75..149, below the known ~70px horizon.
   The highest line is 0; the lowest is 19. No labels/RGB assumptions are added.
   The complete 52-command batch is within the existing 128-call limit.
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
`GrafiteKit` no longer reads colour mappings from a file. The optional
`6wb-kit.palette` file is still used ONLY for the preexisting plane-pair settings;
it is not needed for the chart or colour initialization. The unresolved flat
pair has not been invented or changed by this patch.

## Disable

Set `ENABLE_PALETTE_CALIBRATION = false` in
`src/cluster/hud6wb_calibration.hpp` before the user's next local compilation.
Colours still use the three compiled indices. No env entry or new application
is required. SHADOW follows the same lifecycle and logs commands without sending.

## Checks

Executed in Codex: six new source/data tests, eight kit source/data checks and
two diagnostic-reader Python tests; whitespace/static review. No C++ or QNX
compilation/execution. `test_hud6wb_calibration.cpp` is prepared for subsequent
runtime checking of the exact boundary, duplicate ACTIVE/REROUTING, route restart,
disabled mode, all 20 line payloads and provisional roles.

Remaining F31 checks: actual line visibility/order, meaning of indices 2/14/3,
visible hold duration, normal guidance resumption with updated maneuvers, and
start/stop/reroute behavior. No binary was built or installed by this change.
