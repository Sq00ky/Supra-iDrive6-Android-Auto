# Unified F30 maneuver artwork

The automatically measured **126x77** F30 main-picture profile uses one set of native integer contours for ordinary maneuvers, in both `NBT_HUD_GLYPHS` modes. Far and near views share their arrowheads and bends. Only the incoming tail changes with the four near-route stages; roads supply the near-view context. Other measured sizes, the F20 fallback and explicit canvas/glyph pins keep their existing renderers.

The set contains 18 representative forms: straight, slight/normal/sharp turns, U-turns, on/off ramps, forks, merges and destination. Android Auto aliases retain their family mapping and unknown types retain the straight fallback. The main shaft is 10 native pixels wide; the common source head is 20 pixels long and 26 pixels across, with an 80-degree wing return. Diagonal rounding introduces the expected one-pixel variation. Normal and sharp turns have raised bends and extended outgoing stems. AA9/AA10 have smaller convex outside elbows; the head, inside curve and incoming cap are retained. A minimum-gap regression checks the complete opposing edge segments, keeping the native stem clearance at least 9.8px instead of allowing the elbow to pinch.

The destination is a solid pin body with an independently painted, strictly convex **16-facet** window. MOST's documented polygon command has a single point array, without a hole/contour delimiter. The driver therefore sends two primitives: the body first and the dark window mask last. There are no body triangulation seams or invented compound-contour wire formats. Whether colour 7 masks the white body on the physical cluster remains a car check.

The optional, separately measured **184x17** lane layer uses nine smaller forms in the same style. Lane4–Lane7 share a two-pixel incoming foot. Normal-turn horizontal stems are also 2px; the sharp-turn diagonal edges are 3/sqrt(2), about 2.12px apart after integer rounding. Their heads are symmetric about their own axes, right-hand forms are exact mirrors, and the outside shoulders are clipped without changing the foot width. Unselected directions are painted first, selected directions last. Up to 24 arrows use at most 53 commands, below the terminal's 64-command batch limit. Lane guidance remains disabled by default. Other profiles, custom lane rectangles and an explicit `NBT_HUD_LANES_BOX` pin retain the legacy lane drawing.

## Source and regeneration

- `tools/hud_unified_design.json` is the accepted integer artwork, with canonical left-hand shapes and lane contours.
- `tools/gen_unified_hud.py` precomputes the dark borders, physically clips far-road context, validates simple native polygons and emits `src/cluster/hud_unified_data.hpp`.
- `src/cluster/hud_unified.hpp` selects the family, mirrors right-hand variants and adjusts only tagged incoming-tail vertices. It uses integer operations and static data; no runtime curve sampling or additional library is needed.

From the repository root:

```sh
python3 milestone-7/tools/gen_unified_hud.py
python3 milestone-7/tools/gen_unified_hud.py --check
make -C milestone-7/tests hud-preview hud-matrix
```

The preview decodes the production driver's submitted MOST commands and follows buffer visibility. It writes 32 SVG sheets: both glyph modes, far/all four near stages, CCW/CW ring matrices and selected/unselected lane shapes. They contain 2,906 cells. [Review images](hud-preview/README.md) enlarge these exact polygons without changing their coordinates.

`make check` includes frozen accepted-contour hashes from `tests/fixtures/hud-unified-approved.json`, read back through the actual driver. It also checks generator determinism, inclusive self-contact, exact mirrors, sharp-turn clearance, matching lane widths/heads/corner cuts and the 16-facet pin window, fixed heads and bends, stage tails including their borders, mode/overlay equivalence, lane bounds/fallbacks and real terminal batch admission. Compatibility regressions cover the F20 fallback, other measured sizes and manual pins. Ring/digit tests remain separate and unchanged by the unified ordinary artwork.

The screenshots show polygon previews, not the cluster's rasterizer, palette, text layout or MOST timing. Hardware readability is still to be checked. No firmware deployment is part of generating this catalog.
