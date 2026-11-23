# HUD maneuver review images

These images contain the current production driver's serialized polygon coordinates and colour roles, enlarged for review. Both selectable glyph modes share the [unified artwork](../hud-unified.md) on the automatically measured 126x77 F30 profile.

- [Complete ordinary-arrow catalog](unified-all-arrows.png): all 18 representative forms, far and near stage 4, plus all nine lane shapes. The lane SVG sheets also show both selected and unselected colours.
- [Contour details](unified-contour-details.png): sharp turns with smaller outside elbows and preserved stem clearance, U-turns and the destination's 16-facet window.
- [Lane width and corner details](lane-widths-and-corners.png): Lane4–Lane7 at the same enlarged scale and at native size, with matching two-pixel feet, symmetric heads and clipped outside corners.
- [CCW roundabouts](roundabouts-ccw.png) and [CW roundabouts](roundabouts-cw.png): independently absent/known angles and absent/1..8 exit numbers. Their geometry and digit data are retained.

From the repository root on a configured Linux host:

```sh
make -C milestone-7/tests hud-preview hud-matrix
```

This generates 32 production SVG sheets with 2,906 cells: every Android Auto type 0..60, both glyph modes, far and all four near stages, two-digit exit examples, both ring traffic directions and lane guidance. `make check` verifies coverage and compares ordinary/lane contours against frozen accepted-artwork hashes.

The preview follows actual double-buffer visibility and route updates. It is not a photograph or an emulation of the cluster's palette, rasterization, fonts or MOST timing. The destination uses a solid body followed by a dark window mask; its physical masking behaviour remains to be checked on the car. Lane guidance is optional and remains disabled by default. No deployment was performed for these images.
