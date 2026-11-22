# HUD digit outlines

`src/cluster/hudterm_digits.hpp` contains the original geometric digit outlines selected for the HUD. The logical cell remains 100 by 180, with a 28-unit stem and deliberate diagonal chamfers. The font uses no external font data or runtime library.

Zero, four, six, eight and nine have two simple polygon pieces for their counters; other digits have one. The table has at most 23 points per piece. The corners use integer source coordinates; the renderer fits them only once to the requested pixel height. Adjacent duplicate points, collapsed closing points and collinear backtracking spurs are removed; zero-area pieces are omitted. This cleanup preserves filled area and does not simplify the chosen outlines.

The current 1 has a longer upper hook: its main source segment moves 48 units horizontally and 28 vertically, about 59.74 degrees to the vertical. Its stem is 28 units wide and the lower foot spans 92 units. Returning to the earlier 1 only requires replacing that digit's table; no runtime font switch is needed.

Run `make -C tests run-test_hud_digits` from `milestone-7` to validate the committed source geometry, pixel fitting, counters and pixel fitting. The suite checks 0 through 999 at heights 6 through 40 for bounds, nonzero area, proper crossings and backtracking. At 8 through 40 pixels the outlines are also checked for boundary touches; at the normal 18/22/30-pixel caption heights all counters must remain open. At 6 or 7 pixels some narrow sections collapse to shared boundary points. Tests require neither a font file nor network access.
