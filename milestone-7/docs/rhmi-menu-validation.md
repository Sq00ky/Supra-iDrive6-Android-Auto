# RHMI menu fixes and IDEmu validation

## Corrected behavior

Car manufacturer **Auto** and **Cyrillic street names in HUD** both used action
421. The Cyrillic callback was registered last, so choosing Auto toggled the
Cyrillic flag. Auto retains action 421; Cyrillic uses the otherwise unused action
426 in both the XML declaration and callback registration.

The **Disable Favorites Bar** checkbox also did not read an existing
`favourite-bar.disabled` marker when Settings opened. Its model is now refreshed
on Settings focus, so first entry and re-entry reflect the saved preference.
The flag's meaning and the favorite entry UUIDs are unchanged.

Rebuild the RHMI plugin and install its matching XML together. These fixes do
not change the daemon, text/image databases or phone protocol.

## Host regression checks

```sh
make -C milestone-7/tests run-test_rhmi_actions run-test_settings_refresh run-test_favourite_resources
```

Both new tests failed on unmodified upstream `7ae8d9d` and passed after the
corrections. The action test rejects duplicate callback registrations and
checks the two controls' XML-to-handler mapping. The Settings test compiles
the production refresh/focus functions with a recording model and a real
temporary marker: existing file, removal, recreation, re-entry and focus loss.
It does not substitute a full BMW framework implementation.

On the fresh upstream-based PR tree, the complete Linux GCC 13 host suite
passed, including protobuf schema checks and the session-lifecycle
ThreadSanitizer run. The native image lacked a 32-bit compiler; that check
passed separately in an AMD64 multilib image. Both containers had external
networking disabled. The new regression tests and resource checks also passed
on the macOS host.

## IDEmu observations, 2026-11-21

An isolated stand used the exact `006_016_002 / NBTevo_N18032I` firmware set,
`ID5_1803-510-1749I Build 66`, 2048 MiB, two Cortex-A15 cores and a 1280x480
display. It used the guarded `006016` profile from
[IDEmu PR #16](https://github.com/herrbimmer/IDEmu/pull/16), with no NIC, host
SSH/CAN/GPS bridges, phone or vehicle connection.

The tested bundle used daemon `v1.9.116` and `rhmiapp-93` from the local
`car/combined@38a1700` tree with these two menu fixes. Its RHMI sources and
project include headers match upstream `7ae8d9d` with the same fixes. This is
RHMI provenance, not a whole-daemon runtime test of the newer upstream tree.
The ARM plugin was cross-built with GCC 4.9/QNX 6.5 and matching Build 66
framework libraries. Tested plugin SHA-256:
`e81d003b494145e3ce63664ccac0b87f29c56e2c70a53e9795773b5325fa435c`.

| Check | Observed result |
| --- | --- |
| Action collision | Before: Auto created `hud-cyrillic`. After: Auto saved `vehicle=auto` and left Cyrillic off. |
| Saved Favorites preference | Before: existing marker, unchecked model. After: the checkbox was checked on entering Settings. |
| 18 checkboxes | Each changed and restored its own file without changing the other flags. |
| Eight radio choices | All three cluster types and five manufacturers saved the expected value. |
| About / Version | Pages opened; Version showed `v1.9.116` and `rhmiapp-93`. |
| Cold boot / health | Running in 21.9 seconds; same HMI alive throughout 587 seconds with N57, CarPlay and NBT-AAD libraries loaded. |
| Start / Restart / Force Stop | Default daemon observed for about 67 seconds, Restart replaced its PID, and Force Stop quit cleanly. |
| Open shortcut / USB setting | Shortcut started the daemon; USB was selected. Missing `io-usb` timed out after about a minute, followed by watching fallback accessory nodes. Stop still completed. |
| PF install / check / uninstall | Guest-only operations returned success and restored the original configuration; uninstall disabled the filter as designed. |

## Bounds

These are HMI preferences and process-control checks. No real phone pairing,
USB/AOA or wireless session, projection video/audio, microphone, steering-wheel
input, GPS, physical cluster/HUD or Cyrillic font rendering was tested. The
tested version has no 6WB display driver. Native Screen buffer composition and
USB hardware are not emulated; IDEmu returns an explicit unsupported error for
typed native windows. Daemon survival does not establish video rendering.
Firmware, framework libraries, plugin binaries, screenshots and private
captures are not included with these source changes.
