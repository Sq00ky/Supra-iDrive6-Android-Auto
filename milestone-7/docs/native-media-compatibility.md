# Native Audio media compatibility

The media-list adapter selects a compiled profile by **both** the exact firmware
name and SHA-256 of `HmiMain3D`. A matching model name, HUD setting or familiar
exported symbol is not sufficient. There is no user override that enables a
candidate hook.

| Profile | Firmware | HmiMain3D SHA-256 | Recognized | Emulator evidence | Vehicle evidence |
| --- | --- | --- | --- | --- | --- |
| Build66, high-arm32-v1 | NBTevo_N18032I | `084d0d3236d69bcfbacb2cb73827e2e88c7559e1eca5c1c03ab952805f2b0748` | Exact key only | Build66 / 006_016_002, C495 and C491 | Owner accepted media on BMW F30, 6WA, HUD |

Vehicle/body, cluster and HUD describe the scope of that evidence. They are not
identity keys or a claim of tests on every car carrying the same executable.
The vehicle evidence is for the existing media implementation; a local profile
refactor does not acquire fresh vehicle acceptance automatically.

## Startup

The existing worker computes the fingerprint using the system crypto library's
streaming SHA-256 implementation. The HMI thread does not wait for a whole-file
hash. Shutdown cancels the read. Crypto is loaded dynamically; a missing crypto
library disables the media adapter rather than preventing the PluginApp loading.

The executable is identified through the loader's information for an exported
HMI function. A customary filesystem path alone is diagnostic evidence, not a
verified binding to the running process. Activation additionally requires the
ARM32 executable ABI, required exports, expected export addresses where pinned,
all instruction/vtable anchors, the dispatcher description and native object
guards. Private anchors are read through an independently opened, read-only
`/proc/<own-pid>/as` descriptor; a bad address fails the check rather than being
dereferenced. Unknown identity never reaches those private checks.

The car's loaded `/opt/hmi/bin/zipped/HmiMain3D` is a QNX `S_IFNAM` node,
while its emulator copy is a regular file. Fingerprinting accepts regular files
and named nodes with execute bits and a positive bounded size. Other special
types remain rejected; streaming, exact SHA-256, ELF and runtime checks are
unchanged. The report includes the executable path and metadata failure details.

Export lookup follows QNX's [dlsym contract](https://www.qnx.com/developers/docs/6.5.0SP1.update/com.qnx.doc.neutrino_lib_ref/d/dlsym.html).
[dladdr](https://www.qnx.com/developers/docs/6.5.0SP1.update/com.qnx.doc.neutrino_lib_ref/d/dladdr.html)
identifies the symbol's containing executable; it does not describe private
classes. QNX documents address-space access through
[open, lseek and read](https://www.qnx.com/developers/docs/6.5.0SP1.update/com.qnx.doc.neutrino_prog/process.html).
Neither API proves the layout of a new HMI version.

`native_media_profiles.hpp` owns the verified addresses, export names, windows,
offsets, anchors and evidence. `native_media_hook.hpp` implements the existing
high-arm32-v1 adapter. Another ABI family needs an adapter and tests, not just a
new fingerprint. A recognized entry with neither emulator nor vehicle evidence
cannot activate the private adapter.

## Check compatibility

Open **Settings → Media compatibility → Check compatibility**. The Russian
resource labels are **Совместимость медиа → Проверить совместимость**. The page
shows the current result and the support list. The check runs in the existing
worker and writes `/dev/shmem/nbt-aad-media-compatibility.txt`; its lines also go
through the existing app logger, including USB logging when already enabled.

The report contains firmware, executable SHA-256/ABI/binding, matched profile,
runtime-check result, supported-profile evidence and exported-symbol candidates.
It contains no phone identifiers, VIN, track metadata or process-memory dump.
An unknown executable format can still be fingerprinted for diagnosis, but
cannot use this ARM32 adapter. Export candidates are names/addresses to inspect
offline, not guessed private structure offsets.

For an already verified running adapter, the button reuses its startup identity
and checks the current exports/anchors again. Rehashing the same image would
stall the media-record polling worker unnecessarily. Unknown/failed startup
identity is collected afresh; the report identifies which source was used.

The button does not install, uninstall or retry a hook. Even if an unknown
startup later produces a matching diagnostic, the startup decision remains
unchanged. The app needs a fresh initialization to select a known profile.
Unsupported firmware continues ordinary Android Auto/audio/input behavior;
only the Native Audio media-list decoration is unavailable. No audio-source,
wheel/console, navigation/HUD-artwork or CarPlay configuration is changed.

## Adding a profile

1. Collect the compatibility report and independently inspect that exact HMI
   executable and its libraries. Preserve the full firmware/fingerprint key.
2. Verify the private ABI, each address/offset, exports and independent anchors.
   Reuse the existing adapter only for its verified high-arm32-v1 contract.
3. Add a compiled entry, an evidence-scoped support-text resource and tests.
   Keep recognized/emulator/vehicle evidence distinct; never label candidates as
   physically verified. Run the native lifecycle and real-HMI checks before
   marking the adapter usable. Record vehicle confirmation only after testing.

This stage contains one verified Build66 description. It is not a universal
binary scanner, and does not silently select a nearby firmware version.
