# Mapbox maneuver glyphs on the 6WA HUD

The automatically measured **126x77 F30 profile** now uses the [unified maneuver artwork](hud-unified.md) in both glyph modes, including the optional lane layer. The shared roundabout renderer and digit shapes are unchanged.

For other profiles and explicit canvas/glyph pins, `NBT_HUD_GLYPHS=1` (or `on` / `yes` / `true`) selects the adapted Mapbox glyphs; unset or `0` / `off` keeps the legacy parametric look. The following sections describe that retained renderer. The earlier Mapbox version was car-verified on the F30 6WA on 18.11.2026; the new unified artwork has not been checked on the car.

## Files

- **`src/cluster/mapbox_glyphs.hpp`** — the glyph data: `struct Piece { unsigned char tone, n; short p[8][2]; }`
  and the `kArrow*` / `kFork` / `kMerge` / `kOfframp` / `kArrive` / `kDestination` / `kStarting` / `kRbArrow`
  / `kLane*` tables. **CC0**, from Mapbox's `ManeuversStyleKit.swift` (mapbox-navigation-ios v0.40.0), originally
  copied from the CarPlay plugin's `hudglyphs.h`. The production arrows have adapted wings; the original
  attribution is retained. Points are in the **6WA box** (x 70..170, y 20..120); `tone` 0 = route, 1 = context.
- **`src/cluster/mapbox_hud.hpp`** — the drawing + the maneuver → glyph picker.
- **`src/cluster/hudterm_ring.hpp`** — the shared known/unknown-angle roundabout drawings, used in both glyph modes.

## How it works

1. `arrowPicture()` (`hud6wa_display.cpp`) sends roundabouts to the shared ring renderer first. For ordinary
   maneuvers, `mbxManeuver(spec, profile, pic)` selects Mapbox when enabled; otherwise the parametric drawing is used.
   The maneuver reaches it as `ShapeSpec::aa_type` (set by `shapeFor`).
2. `mbxGlyphForFamily(kombiDirection(aa_type))` picks the glyph by the **same families** the parametric path uses
   (`kombiDirection()` in `nav_msg.hpp`): straight, slight/normal/sharp turn, U-turn, fork, off-ramp, on-ramp/merge,
   destination. The **left** variants are the right glyph **mirrored** (x reflected around the 6WA centre, 120).
3. `mbxGlyph()` maps each `Piece`'s 6WA-box points into the profile's `glyph` rect — the same linear fit the CarPlay
   `hud_box` uses, `x0 + (x-70)*(x1-x0)/100` — so the arrow lands exactly where the car-tuned box is. Division is by
   the constant extent (100) only, so no `__aeabi` integer-divide is emitted (keep it that way). `tone` 0 → the fill
   colour (`profile.fill`, our `NBT_HUD_FILL=5` orange), 1 → the context tone (`profile.road`, grey).
4. Roundabouts keep the same drawing far and near. A known angle has a radial exit arrow, with the supplied valid
   number in the centre; 360 degrees has separate incoming and returning branches. An unknown angle shows
   three smooth circular arrows with tangent heads, a plain incoming bar and the supplied number, without
   inventing an exit direction. Neither the glyph mode nor
   the exit number changes the angle. Known inputs 1..29 are drawn at 30 degrees and 331..359 at 330; 360 keeps its
   returning branch, and the raw input stays in the diagnostic log. The older low-level `mbxRoundabout()` helper remains covered separately.

Android Auto types 44/46 are a separate departure step starting inside the roundabout. Both glyph modes draw a
partial circular road with an outgoing arrow, mirrored for clockwise travel. This picture uses the main buffer at
all distances, without a caption or an entry-relative exit angle. Other roundabout types keep their approved
geometry, fitting and digits. The diagnostic reads `roundabout exit cw/ccw` for this phase.

Unknown-angle arrows follow circular bands with tangent 90-degree heads and a plain entrance of the same width.
They use a separate fitting envelope so their new outline cannot move a known-angle ring. At the reference
66-unit scale, single-digit unknown captions stay 24px high; two-digit captions use 15px to clear the tangent
heads. Below that scale they use one additional pixel of vertical inset, and captions below 6px are omitted.
Known-angle caption sizes remain 24/18px. Known circular bands
retain their outer 7.5-degree samples and use denser 2.5-degree inner samples. The previous radii, entrance, exit
head, caption, fitting anchors and number of drawing commands are retained. Each half-band has at most 98
points before native-grid deduplication, within the terminal's 99-point limit.

## Arrow wings

The lateral protrusion beyond the shaft is reduced by 30%. The return edge of each wing meets the shaft axis at
80 degrees. The tip, neck, shaft and longitudinal length retain their original coordinates. This applies to straight,
slight, normal and sharp turns, U-turns, forks, merges and ramps, plus the older Mapbox roundabout helper. The shared
known-angle ring renderer uses the same wing rules on a compact radial head. Destination and unused drawings are unchanged. Pixel rounding
introduces a small angular error at the HUD's final size.

The legacy Mapbox path fits its source bounds through `mbxOriginalBounds()`. Refitting to the narrower wing bounds would enlarge or move the whole icon. Its sharp turns and U-turns retain fractional source coordinates until fitting; off-ramps keep a constant-width head join. See the unified-artwork document for the automatic F30 replacement and its native-grid contours.

The shared roundabout exit retains its 8-pixel external shaft and circle diameter.

Regenerate the committed data from the repository root, using Python's standard library only:

```sh
git show cd1d9f8:milestone-7/src/cluster/mapbox_glyphs.hpp > /tmp/mapbox-original.hpp
python3 tools/gen_mapbox_wings.py /tmp/mapbox-original.hpp
```

The generator verifies its original input and updates the derived header and test fixtures. The tests run against
the committed data without requiring Git history, network access or a font library.

## Visual review and tests

From `milestone-7`, run `make -C tests hud-preview hud-matrix` to generate the production driver's serialized
polygons for both glyph modes and all four near-route stages, plus the far view. The ordinary sheets include every
Android Auto type 0 through 60 and two-digit exit numbers. The ring matrices independently vary angle (absent or
45 through 360 degrees), number (absent or 1 through 8), and traffic direction.

`make check` also checks the catalog coverage, visible arrow size, mirrored shapes, near-stage transitions, packet
budgets, geometric digit contours and the Mapbox wing anchors. A two-translation-unit regression ensures the curved
body lookup does not depend on the address of a header-local glyph array. Native-grid regressions cover unknown
rings at sizes 1..200, both traffic directions and captions 1..99, checking self-crossings, backtracking and contact
between numbers and arrows. A frozen known-angle regression preserves layouts, entry/exit contours and captions
while allowing denser sampling of the circular bands. See `hud-digits.md` for digit tests.

## Not done / how to extend

- **Ferry** has no glyph → folds to the straight glyph via `kombiDirection` (unchanged from before).
- **Lanes**: the `kLane*` Mapbox tables remain unused. The automatic F30 lane strip uses the unified artwork; other profiles retain their legacy drawing.
- **Update the source artwork**: import the CarPlay plugin's `tools/hud_glyphs.py` output, preserve its attribution,
  and review the wing anchors before adapting a new version. The wing generator rejects an unexpected source file.

## Build & deploy

Built with the GNU QNX toolchain (see the `nbtaad-qnx-build` notes: dynamic libstdc++ + `compat_stdcxx.cpp`,
`-include cstddef`, head's OpenSSL 1.0.1l by path). Deploy: atomic `mv` over `/opt/aa/nbt-aad`; reconnect AA to apply.
