# 6WB dual startup calibration

Current implementation: [6WB_DUAL_CALIBRATION_FLAT.md](6WB_DUAL_CALIBRATION_FLAT.md).

After the existing C04 validation, one sequence runs per new NAV route:

1. Palette 0..19 and BITMAP TEST 1: ten-second monotonic hold.
2. Left/right stepped rulers and BITMAP TEST 2: ten-second monotonic hold.
3. Clear both visible surfaces and resume the latest normal NAV snapshot.

Each hold starts after observed local dispatch completion. Dispatch/scheduling
add finite transition time, so software holds are not measured panel-light
presentation timestamps. No sleeps, new worker, Ack wait or retry is introduced.
Duplicate ACTIVE/reroute does not restart the sequence; existing cancellations
remain. Set ENABLE_PALETTE_CALIBRATION=false to disable both tests.

Palette samples retain exactly the 1574be1 coordinates:

| Indices | Y |
|---|---|
| 0–3 | 0..17 |
| 4–7 | 37..54 |
| 8–11 | 74..91 |
| 12–15 | 111..128 |
| 16–19 | 148..165 |

X columns: 25..64, 73..112, 121..160, 169..208. No horizon or extra offset.
The separate reference bars now use expected-white index 6. Geometry is unchanged;
transparent sample positions remain identifiable independently of their fills.

Both central tests use only 1F/20; upper bitmaps alternate 18/19. All four
surface contents are restored/hidden through the normal ordered lifecycle.
See the detailed document for all 41 step coordinates, nine right-side markers,
NBT_RLE32/C05 layout, literal bitmap colours and exact 64/106-call budgets.
Physical visibility and vector colour meanings remain experimental.
