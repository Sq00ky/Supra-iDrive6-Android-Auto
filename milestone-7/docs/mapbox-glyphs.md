# Mapbox maneuver glyphs on the 6WA HUD

Draw the polished Mapbox maneuver glyphs on the cluster HUD instead of the parametric shapes
(`hudterm_shapes.hpp` `drawShape`). On by default; `NBT_HUD_GLYPHS=0` in `nbt-aad.env` falls back
to the parametric shapes (to compare on the car). Car-verified on the F30 6WA cluster, 18.11.2026.

## Files

- **`src/cluster/mapbox_glyphs.hpp`** — the glyph data: `struct Piece { unsigned char tone, n; short p[8][2]; }`
  and the `kArrow*` / `kFork` / `kMerge` / `kOfframp` / `kArrive` / `kDestination` / `kStarting` / `kRbArrow`
  / `kLane*` tables. **CC0**, from Mapbox's `ManeuversStyleKit.swift` (mapbox-navigation-ios v0.40.0); copied
  verbatim from the CarPlay plugin's `hudglyphs.h` (the same set the CarPlay HUD uses), only the namespace
  changed to `nbt::hudterm::mbx`. Points are in the **6WA box** (x 70..170, y 20..120); `tone` 0 = route, 1 = context.
- **`src/cluster/mapbox_hud.hpp`** — the drawing + the maneuver → glyph picker.

## How it works

1. `mbxManeuver(aa_type, profile, pic)` is called first in `arrowPicture()` (`hud6wa_display.cpp`). The maneuver
   reaches it as `ShapeSpec::aa_type` (set by `shapeFor`), so no call signatures changed.
2. `mbxGlyphForFamily(kombiDirection(aa_type))` picks the glyph by the **same families** the parametric path uses
   (`kombiDirection()` in `nav_msg.hpp`): straight, slight/normal/sharp turn, U-turn, fork, off-ramp, on-ramp/merge,
   destination. The **left** variants are the right glyph **mirrored** (x reflected around the 6WA centre, 120).
3. `mbxGlyph()` maps each `Piece`'s 6WA-box points into the profile's `glyph` rect — the same linear fit the CarPlay
   `hud_box` uses, `x0 + (x-70)*(x1-x0)/100` — so the arrow lands exactly where the car-tuned box is. Division is by
   the constant extent (100) only, so no `__aeabi` integer-divide is emitted (keep it that way). `tone` 0 → the fill
   colour (`profile.fill`, our `NBT_HUD_FILL=5` orange), 1 → the context tone (`profile.road`, grey).
4. No glyph for a family (**roundabout**) → `mbxManeuver` returns false and `arrowPicture` draws the parametric shape.

## Not done / how to extend

- **Roundabout** stays parametric (`drawRoundabout`). To use the Mapbox roundabout, port the CarPlay plugin's
  `hudshapes.h` roundabout: `kRbArrow` + the `arc()`/`polar()`/`boxX()`/`boxY()` helpers (Mapbox's 512-unit canvas,
  `boxX(v)=70+((v*25+64)>>7)`), with the exit arrow rotated to `ShapeSpec::exit` (the exit angle AA already gives us).
- **Ferry** has no glyph → folds to the straight glyph via `kombiDirection` (unchanged from before).
- **Lanes**: the `kLane*` tables are in `mapbox_glyphs.hpp` but the lane strip still uses its own drawing; wire them
  in `lanesBatch` if wanted.
- **Regenerate / update glyphs**: run the CarPlay plugin's `tools/hud_glyphs.py` against the Mapbox SVGs to produce a
  new `hudglyphs.h`, then copy it over `mapbox_glyphs.hpp` and re-apply the namespace change.

## Build & deploy

Built with the GNU QNX toolchain (see the `nbtaad-qnx-build` notes: dynamic libstdc++ + `compat_stdcxx.cpp`,
`-include cstddef`, head's OpenSSL 1.0.1l by path). Deploy: atomic `mv` over `/opt/aa/nbt-aad`; reconnect AA to apply.
