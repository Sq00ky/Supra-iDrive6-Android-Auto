# Native Audio in the F30 high media list

The Build66 adapter replaces two existing placeholder rows with the current
Android Auto title and artist while Browser Audio owns the media route. It uses
the existing native high publisher and subscribers. Wheel input and audio-source
selection are unchanged.

## Two different native states

| Window | Native role | Observed Russian rows |
| --- | --- | --- |
| `C495` | `CDMM_NO_AUDIO`, mode49: no active entertainment source | `Нет активного` / `источника развл.` |
| `C491` | `CDMM_BT_NotAvailable`, mode20: external Bluetooth media unavailable | `Внешние устр-ва` / `отсутствуют` |

These are native HMI states of the same media-list mechanism. The wording does
not diagnose the Android Auto USB connection. Browser Audio can be playing
successfully while the factory external-device list remains empty.

The exact window mapping comes from the saved Build66 HMI and GUI data, not
from a public BMW specification. The mode-name table includes
`CDMM_USB_NotAvailable`19, `CDMM_BT_NotAvailable`20 and `CDMM_NO_AUDIO`49.
The native constructor at `0x00F85ADC..0x00F85AFC` creates `C491` and stores
it in the mode20 slot at app+`7A8`. The independent IC-thread observation after
selecting stock Bluetooth returns window`C491` and audio mode20. The adapter
does not decorate the generic media/radio notice windows `C43D`/`C43C`.

The actual F30 capture while Native Audio played contained two rows with
IDs1/2, type2, formats1/1 and symbol0, a registered native subscriber, and
the two external-device labels above. Its native Info fields at offsets
`2C/44/48` were `3/1/3`; the saved GUI defines `C491` with
ListIdentifier3, ListFormat1 and ListColor3. These fields are also shared by
other windows and cannot identify the context alone. The previous emulator fixture
opened only `C495`, whose corresponding values are `0/1/2`. The earlier
`d7e436b` guard therefore omitted the real-car media-placeholder case.

The real-car snapshot contains cached mode20 in the pinned audio-mode controller.
`C491` additionally requires this exact mode. Other modes, including USB19
and rear-control priority50, are rejected even with two rows. Generic `C43D`
can carry rear-control notices and is excluded entirely. Android Auto over USB
is a separate transport from the factory USB media-device state.

Both accepted windows still require the exact two-row shape, a supported HMI,
fresh complete metadata, playback and a granted Native Audio media route.
Localized label text is never used as the guard. Radio, telephone, real device
lists, changed row layouts and other HMI builds retain their native behavior.
Stopping, pausing or losing the Native Audio route restores each unchanged
adapter-owned row to its saved native value.

## Settings and upstream documentation

- **Use Native Audio instead of Bluetooth** selects Browser Audio. The upstream
  author describes why this source does not automatically supply the factory
  Bluetooth media UI in [issue163](https://github.com/Sq00ky/Supra-iDrive6-Android-Auto/issues/163).
- **Song title in gauge cluster** is a separate navigation-street-line feature
  on6WA. The [v1.9.116 release notes](https://github.com/Sq00ky/Supra-iDrive6-Android-Auto/releases/tag/v1.9.116)
  distinguish it from J29's native track-list support. It is not required for
  the F30 high-list adapter and can remain off.
- **Native Functionality Media Controls Restore** remains as configured; this
  patch does not change wheel or console controls.
- **Nav prompts mixed into Music** changes prompt audio routing, not native
  media-list recognition or Unicode metadata.

BMW's public [Bluetooth audio FAQ](https://faq.bmwusa.com/s/article/Connectivity-Bluetooth-Bluetooth-Audio-k8vpo?language=en_US)
describes external Bluetooth devices and their activation in MEDIA. It does not
document these internal window IDs or the aftermarket Browser Audio adapter.

## Display and validation

The supported native window keeps its existing two label rows: title first,
artist second, each limited to 25 UTF-16 units without splitting a surrogate pair.
This is not the Bluetooth previous/current/next layout; selectable rows and
selection highlighting are a separate feature.

An F30 owner confirmed current Cyrillic title and artist in the HUD. With the
engine stopped and NBT, Android Auto and music still running, the same active
metadata appeared in the instrument cluster after the HUD switched off. This
does not establish identical display dimensions or test cleanup after AAD stops.

Host tests cover metadata parsing, freshness, source changes, daemon shutdown,
private-hook lifecycle and conditional restoration of individual native rows.
The exact Build66 emulator also exercises both native windows with the production
plugin and verifies restoration on pause, source loss, expiry and session stop.
Other HMI builds fail closed; no navigation artwork or input dispatch is changed.
