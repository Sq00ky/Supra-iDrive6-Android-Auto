# Native Audio in the F30 high media list

Firmware selection and diagnostic behavior are described in
[Native media compatibility](native-media-compatibility.md). The Build66
addresses and private layout now live in a compiled compatibility profile.

The Build66 adapter replaces two existing placeholder rows with the current
Android Auto title and artist while Browser Audio owns the media route. It uses
the existing native high publisher and subscribers. Raw input decoding and
audio-source selection are unchanged.

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
fresh complete metadata, a Playing/Stopped status and an open stream on the
granted Native Audio media route.
Localized label text is never used as the guard. Radio, telephone, real device
lists, changed row layouts and other HMI builds retain their native behavior.
Stopping, pausing or losing the Native Audio route restores each unchanged
adapter-owned row to its saved native value.

A partial in-place metadata write is rejected without withdrawing the last
complete view. Its original freshness deadline still applies. A complete inactive
record or removal of the record file withdraws the view immediately.

## Bluetooth-style presentation

This feature adds the stock Bluetooth high-list vocabulary: previous/current/next
rows, native skip symbols and a selected row. The current title occupies the
centre row; the artist uses the native list header. Each text is bounded to
25 UTF-16 units. The accepted two-label implementation remains the fallback.

The factory C491/C495 model stays two-row. Only outgoing native properties use
temporary three-row views. Content, position and list information are published
with the same native generation to the same existing subscriber cohort. Empty or
different subscriber lists retain the two-label view. A partially failed send
discards the richer view and schedules one fresh native-generation redraw.
The adapter also answers native Content and Info Gets from the last committed
view, preserving row-ID, selector and requester semantics. Position Get retains
the factory's unsupported response. Subscription initialization and repeated
subscriptions use the same cached position count and focus as the rich content,
including the Position property's separate mode0 publication path.
If the metadata reader holds its lock during publication, the IC thread uses
the committed frame while it is still fresh and schedules a retry. This applies
to Get responses and complete native update transactions; it does not extend
playback freshness or retain a partially failed transaction.
Yandex Music can report `stopped` between titles while its Native Audio stream
and route remain open. That status retains the list and selection, including
when new metadata precedes `playing`. Closing the stream, losing its route,
missing/expired metadata or stopping AAD still withdraws the list immediately.

Wheel decisions and Android Auto key dispatch still have one owner. The daemon
shares the Native Audio previous/next choice with the existing MOST wheel-press
handler and mirrors it, with the last wheel activity time, into the metadata record.
Closing the popup keeps the chosen button; the informational title is never selected.
Before the first selection, Next is shown and used by a press. Playback heartbeats
do not reopen the popup. Pause, source loss, player change, metadata expiry
and shutdown clear the presentation state. No native Bluetooth or CarPlay input
controller is activated and no additional key callback is installed.
Previous sends the existing Android Auto Previous command once; restart-from-beginning
versus previous-track behavior remains the phone player's decision.

The instrument cluster and HUD consume this native high-list protocol. Exact
header placement, orange highlighting and truncation remain physical display
checks; a virtual MOST consumer verifies the emitted layout and selection but
does not emulate the cluster's own renderer.

## Settings and upstream documentation

- **Use Native Audio instead of Bluetooth** selects Browser Audio. The upstream
  author describes why this source does not automatically supply the factory
  Bluetooth media UI in [issue163](https://github.com/Sq00ky/Supra-iDrive6-Android-Auto/issues/163).
- **Song title in gauge cluster** is a separate navigation-street-line feature
  on6WA. The [v1.9.116 release notes](https://github.com/Sq00ky/Supra-iDrive6-Android-Auto/releases/tag/v1.9.116)
  distinguish it from J29's native track-list support. It is not required for
  the F30 high-list adapter and can remain off.
- **Native Functionality Media Controls Restore** enables the existing wheel
  media handler. Its Native Audio choice now follows the remembered upper/lower
  button; raw input decoding, console keys and single key dispatch stay unchanged.
- **Nav prompts mixed into Music** changes prompt audio routing, not native
  media-list recognition or Unicode metadata.

BMW's public [Bluetooth audio FAQ](https://faq.bmwusa.com/s/article/Connectivity-Bluetooth-Bluetooth-Audio-k8vpo?language=en_US)
describes external Bluetooth devices and their activation in MEDIA. It does not
document these internal window IDs or the aftermarket Browser Audio adapter.

## Validation scope

The owner accepted the BT-style list, title/artist, remembered previous/next
selection and track changes on BMW F30 / 6WA / HUD with Build66 / NBTevo_N18032I.
The acceptance came from the integrated local candidate `9c90ca5`, with the
`df316e6` media library. This main-based PR carries the same adapter, record
format and compatibility profile; its new commit is not a separate car test.

Navigation graphics, roundabouts and native speed-bar suppression are maintained
in separate PRs #317, #318 and #346. They are not included in this media patch.
