# HUD maneuver review images

These images capture the production renderer at `a81874b` on the measured 126x77 F30 HUD. The sharp-turn before/after uses `43f916d` as its baseline. The captions are in English; polygon coordinates and colours are copied from the serialized driver output without alteration. The display is enlarged for review.

- [Sharp turns before/after](sharp-turns-before-after.png): AA9/AA10, both glyph styles, near stage 4. The same head clearance is checked at all four near stages.
- [Far/near comparison](maneuvers-far-near.png): normal turn, sharp turn and U-turn, at far / near 1 / near 4.
- [CCW roundabouts](roundabouts-ccw.png) and [CW roundabouts](roundabouts-cw.png): absent/known angles independently combined with absent/1..8 exit numbers. The ring picture is the same far and near.

To generate all 30 production SVG sheets, run from the repository root on a configured Linux host:

```sh
make -C milestone-7/tests hud-preview hud-matrix
```

See [Mapbox glyphs](../mapbox-glyphs.md) and [digit outlines](../hud-digits.md) for the renderer and its regressions. These are previews of integer polygons, not photographs or an emulation of the cluster's palette, rasterization, fonts or MOST timing. Physical HUD readability remains to be checked.
