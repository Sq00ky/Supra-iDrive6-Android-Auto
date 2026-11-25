# Local F30 caption and night-outline correction

This patch centres the final integer caption ink bbox on the ring centre, translating it as a whole. Size, per-digit contours, ring, heads, branches and placement remain unchanged. Odd native bbox extents allow at best half-pixel centre error. Tests cover all angles, both directions, captions1..99, bbox/clearance and translation-only shape preservation. The frozen single-digit fixture was independently translated from the accepted reference, without reading the C++ port.

Existing iDrive day/night reaches the phone through main. The same valid state now reaches the HUD worker under its existing mutex. State1 means day,2 night;0/3/unknown retain the last valid state. Initial unknown keeps the existing daytime rendering. No RHMI, wheel, BT or audio code was changed.

At night only, the automatic F30 renderer skips body/head/road border polygons. Day rendering and palette indices stay unchanged. The destination pin window is a separate interior mask, not an outline; it remains pending dark-index calibration. A state transition invalidates the current drawing, including the near route overlay. Other profiles/pins retain the legacy renderer.

**Night road colour is NOT fixed or physically verified.** Index3 remains unchanged; index7 appeared white to the owner on F30 and must not be treated as black based on F20 notes. No RGB-to-index mapping is guessed. This patch alone does not deliver the complete orange/grey night target.

## Minimal car calibration, after separate authorization

1. Stationary, with other navigation/HUD publishers inactive. Preserve current HUD brightness and every tuning flag. Confirm the measured frame126x77 and car day/night signal (CDS1 day,2 night) in the existing log; do not infer state from the phone theme.
2. On the verified main picture handles5/6 and panel1 only, show one temporary sheet with eight separated flat rectangles: top row indices0,1,2,3, bottom4,5,6,7. Position legend must be external, so index0/black remains identifiable. Do not send to unknown handles or change coding. This is a plan, not an installed probe or authorization to send commands.
3. Photograph the same sheet once in confirmed daytime and once in confirmed night mode, at unchanged brightness. Record physical colours/relative brightness of indices3,4,7 and the remaining candidates. In particular identify the desired dim grey and a genuinely dark mask; a white-looking index is not grey by assumption.
4. Restore the normal display. Only after those observations, map night road/arrow/mask roles to measured indices, keep the accepted daytime roles, and test far/near/roundabout plus night-to-day redraw. If no dim neutral index exists, report the limitation rather than simulate a colour that the hardware does not provide.

Local only: no vehicle access, publication or combined firmware build is part of this patch.

## F30 calibration and local pen candidate, 2026-11-26

The later stationary swatch check supersedes the earlier grey-road target and main-only calibration plan above.
Main buffer6 and near-route buffer8 had matching colours: index4 is white by day/orange by night;3 is dim/light
orange;7 is white. No grey was found in the tested night range0..31. Indices16..31 were not visible, with a
visible0..7 control confirming the second half of the image/item slots. Day0..7 was checked; day8..31 was not.
Buffers5/7 answered126x77 but were not separately colour-calibrated. Invisible0/1 on an empty background does
not establish alpha, black masking or erasing behaviour.

The owner then chose white active arrow/path with orange road by day, orange active arrow/path with light-orange
road by night, including the ring. Existing fill4/route4/road3 already express those roles, so their values and
the accepted ring/caption contours remain unchanged. A hollow background road is a separate, unimplemented proposal.

The remaining local candidate initializes LineWidth0 after clearing each nonempty automatic F30 route buffer7/8,
before adding its polygons. This happens in both schemes and on every reuse, including after another publisher;
there is no unknown pen state to restore at a night-to-day transition. Explicit daytime edge polygons remain in
place; main5/6, ring/caption, lanes, other profiles and manual pins are untouched. Near route batches gain one call
(typical night6→7, day9→10), within the existing budget. Native LineColor/FillMode are not guessed.

The regression uses an adversarial retained pen and covers both buffers, reuse, day/night and scope guards.
It fails on the unchanged base specifically because no zero-pen setter precedes the route polygons. Separate
colour-role assertions cover ordinary route stages and both directions of a known-angle numbered ring. This
is a driver contract test, not a model of the physical cluster's rasterizer.

**The photographed white near-arrow cause remains unproven.** Matching swatches do not establish the image's
retained pen/fill state, and this candidate has not been run on the car. CarPlay's6WA path and hudprobe initialize
LineWidth0, which supports the change but does not verify its physical result. Do not describe colours as fixed.

Before accepting a combined build on the car, check an ordinary near turn at confirmed night and day, both route
buffers through stage changes, and night→day→night. Require the zero-width setter and complete route ACKs;
check body/head fill, fringes, visibility and unchanged geometry. Verify far/near and an accepted numbered ring;
stop the experiment if the arrow disappears, a setter is refused, or white fill remains. If it remains white,
capture factory/native paint attributes rather than selecting a different unmeasured palette or FillMode.
No installation, publication or HMI restart is part of this local candidate.
