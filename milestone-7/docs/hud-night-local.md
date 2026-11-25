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
