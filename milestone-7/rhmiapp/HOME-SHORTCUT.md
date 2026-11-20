# Experimental dashboard shortcut

Enable **Home shortcut (CarPlay icon, experimental)** in Android Auto Settings.
It is off by default. On the profiled ID6L firmware, while Android Auto is
connected and native CarPlay is unavailable, the factory CarPlay dashboard
widget becomes an entry back into Android Auto. Its icon and focused caption
remain **Apple CarPlay**. No factory artwork, coding or connection state is changed.

The setting applies on the next HMI polling tick without restarting the phone
session. Its explicit 0/1 value is saved atomically in `home-shortcut.setting`
in the selected data directory: `/var/opt/aa` for manual installations, or
`/var/opt/nbtapp/data/com.github.sq00ky.android-auto` for packaged installations.
Missing or invalid settings mean disabled. No legacy enable flag is read.

## Compatibility and limits

Visibility and selection were verified on one NBT Evo ID6L firmware profile.
Other firmware and ID5 layout have not been verified. Unknown profiles are
rejected; this is not universal NBT Evo support. The normal Media/Radio entry
remains independent. The menu preference and the upstream integration need
an in-car regression test before treating this as a stable feature.

Profile evidence: HmiMain3D size 59,792,948 bytes, SHA256
`6e2267a592e8b6b8b7c66a70e1f0d4eedced798ddc90866793d82c89a85a3878`.
Runtime checks inspect executable/library sizes, exported anchors, instruction
samples and RTTI/vtables; they do not compute the full executable SHA256.
No proprietary binaries are included.

## Implementation

`home_shortcut.cpp` resolves native services, checks the profile and schedules
callbacks on CHmiAppProcUnit. The library is pinned for the HMI lifetime so
callbacks cannot refer to unloaded code. Reboot after updating the library.
There is no generic menu-entry fallback or additional artwork to install.

`home_visibility.cpp` uses the native CarPlayVisible scene-model setter. It
reads actual CarPlay availability without changing the CarPlay service state.
Native CarPlay takes precedence when available.

`home_selection.cpp` obtains CHmiSceneManager through the native HMI-thread
dependency cache and observes ID_SIGNAL_CLICKAPPLECARPLAYTOUCHHOOK. It retains
the original handler and queues the existing PluginApp startDaemonOrShow path.
Disconnected AA, a disabled setting, actual CarPlay availability and comparison
mode prevent the observer from taking an AA action.

The existing `/dev/shmem/nbt-aad-home-probe` marker requests read-only comparison
instead of AA visibility/selection changes. Diagnostics use `[Home]` log lines.
