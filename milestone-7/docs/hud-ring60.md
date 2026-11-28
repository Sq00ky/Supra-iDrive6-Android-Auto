# F30 roundabouts: approved 60px ring

The automatically measured 126×77 HUD, without explicit canvas/glyph pins, uses the same native artwork in both glyph modes and at every distance. Other measured sizes, F15, the F20 fallback and manual pins retain their previous renderer.

- The circle stays 60px across. Its nominal band and branch width is 7.425px; integer fitting gives 7–8px locally. Outer and inner arcs share convex lattice contours, avoiding small radial dents from independent sample rounding.
- Known angles have radial exits. The exit head is 9.1667px long, one third shorter than the original, with its 16.4766px wing span preserved. Its whole head sits outside the circle. Entrance and external exit neck extend up to 6px and 5px respectively; near 180° their extensions decrease to fit the frame. The accepted 180° keeps its previous entrance, exit and centre.
- At 360° the two parallel branches remain separate. The entrance now ends at the same height as the outgoing tip. The centre moves vertically to fit; the ring does not shrink.
- Missing/invalid angles show three circular arrows and a plain entrance. The 18×10px circulation heads are unchanged. A number alone never invents an exit direction. Inputs 1..29 display at 30°, 331..359 at 330°, and exactly 360° stays distinct.
- Single-digit captions are the accepted native outlines enlarged 25%, from 24 to 30px high, and remain upright in both traffic directions. Two-digit labels use the smaller caption (about 22px high) to retain clearance. Existing geometric font outlines remain unchanged.

## Source and checks

`tools/hud_ring60_design.json` stores the accepted integer artwork: shared circles, circulation, exit heads and per-angle placement. Like the ordinary F30 artwork, its generated header is static data. The daemon requires neither floating point nor another graphics/font library.

```sh
python3 milestone-7/tools/gen_ring60_hud.py
python3 milestone-7/tools/gen_ring60_hud.py --check
make -C milestone-7/tests run-test_hud_ring60 run-test_hud_ring60_golden
make -C milestone-7/tests hud-preview hud-matrix
```

The frozen fixture was extracted from the visually accepted native design before the C++ port. It checks all 7,220 combinations of raw angles 0..360, absent/1..9 captions and both directions. Native C++ checks cover bounds, simple polygons, exact mirroring, 360° branch alignment and caption clearance for 1..99. Driver integration verifies serialized output, profile fallback, measurement changes, pins, near/far equivalence and terminal admission.

## Day/night colour proposal

The selected target appearance is a white arrow with orange road context by day, and an orange arrow with dim white context by night. The same proposed orange, `#E6A064`, is used in both modes. The native HUD protocol sends palette **indices**, not arbitrary RGB. No palette mapping, day/night switching, global brightness or ordinary-arrow outlines are changed by this geometry update; hardware palette calibration is still required before those colours can be promised on a car.

The [colour sheet](hud-preview/ring60-day-night.png) is built from production-driver coordinates. Reproduce its HTML after building `hud_preview`:

```sh
python3 milestone-7/tools/hud_ring60_colours.py \
  milestone-7/tests/build/hud_preview /tmp/hud-ring60-colours.html
```

The browser joins consecutive same-colour polygon paths for one fill. This removes preview-only antialiasing seams in multipart digits such as 6, 8 and 9 without changing native vertices or inventing compound-contour MOST commands. The sheet is not an emulation of the cluster's rasterizer or physical brightness.
