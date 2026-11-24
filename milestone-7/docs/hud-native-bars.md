# Compact HUD native speed bars

On a compact F30 HUD, the cluster's own bar instances can remain visible over the speed while AAD draws guidance or music. AAD already supports the manual `NBT_HUD_HIDE="21 22"` override. This change adds automatic suppression for the confirmed compact profile, without changing glyphs, music records, or steering-wheel commands.

Automatic hides require all of the following:

- Fixed roles: frame/street/distance/unit/buffer A/buffer B = 1/2/3/4/5/6.
- A solicited normal `ImageGetSize` ResultAck from the current primary 82/00 host, addressed to the current registered client, for handle 5. Six-byte replies are accepted; seven-byte replies must carry marker `01`. Dimensions must be 24..1023 and exactly **126x77** for this capability.
- AAD's frame is on and the terminal is ready, outside the existing native-navigation hold.

A manually pinned canvas or glyph is not proof of the terminal's size and does not disable a genuine size reply. The legacy geometry parser and cached fitting size are kept separately: Status replies and other legacy forms do not grant hide permission.

After a successful main frame-on or distance-update batch, Core schedules a separate, bounded batch with `SetVisibility` payloads `001500` and `001600`. It uses the existing eight-call window and does not increase the caller's drawing-batch limit. Manual hide permissions keep their old path and are not duplicated. Clear/frame-off does not schedule the automatic pair. Arbitrary submitted commands still cannot use the guessed handles: only this internally constructed visibility-off pair gets the narrow permission.

Each automatic call is rechecked after acquiring MOST's send lock, immediately before dispatch: current registered source, primary destination/instance, measurement generation, ready state, and the active frame. A takeover during the lock wait or between the two hides cancels remaining automatic work. A failed send removes its own recent-copy entry. This is a guard for these new commands, not a rewrite of general HUD ownership or a claim of cross-application compatibility.

Attach/detach, registration (including the same address), primary-host changes and guidance-location changes invalidate hide proof and the previous frame-on. The cached geometry remains available where it did before; the existing one-shot size mechanism is rearmed for the new identity. There is no periodic probing. A late valid answer advances the display epoch so an unchanged maneuver or music view redraws, including when canvas/glyph pins keep its fitted rectangle unchanged. Size replies have no transaction identifier: an old reply that is byte-identical to a fresh outstanding request on the same host/address cannot be distinguished by this protocol.

Run the focused tests on the configured Linux host:

```sh
make -C milestone-7/tests run-test_hud_native_bars run-test_hud_terminal run-test_hud6wa run-test_most_client run-test_kombi_nav
```

The new test covers strict reply forms and provenance, manual/unknown profiles, reset and registration epochs, late replies with pinned geometry, guidance and media redraw, a 32-call drawing plus its separate two-call hide batch, frame-off, and deterministic takeover/send-failure barriers through the real Client send lock. Fake DSI replies are host-only evidence; no physical disappearance of the bar is claimed until the owner checks the new build on the car.
