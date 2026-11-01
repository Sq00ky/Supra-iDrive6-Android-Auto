# NBT-AAD: native wireless Android Auto for NBT Evo (ID6) head units

Wireless Android Auto running natively on the BMW NBT Evo ID6 head unit, as fitted to the Toyota GR Supra (J29)
and several BMWs. A daemon on the head unit (`/opt/aa/nbt-aad`) speaks the Android Auto protocol directly. A native
iDrive app (`NbtAad`) adds an **Android Auto** entry to the Media/Radio menu. You don't need a dongle, a phone-side
companion app, developer mode, or "Start head unit server". iDrive's own binaries are not patched or hooked.

**Version:** daemon `v1.8.47-gesture3`, iDrive app `rhmiapp-22` (Fork F, the main tree), plus the fixes in [FIXES.md](FIXES.md). This folder is milestone-7.

> **v1.8.42-stability / rhmiapp-20 (not car-verified):** after a tester's head unit reset 5 times in a row
> (phone ending every session right after setup, `audioconn` requests piling up until memory ran out; fixed in
> v1.8.41-routeguard): the phone's `ByeByeRequest` is answered with `ByeByeResponse` and ends the session at once
> (logged with its reason); three sessions in a row that end within 15 s print a hint to fetch the phone's logcat;
> session, `audioconn` and shutdown lines carry uptime timestamps (same clock as the iDrive app log) and every shutdown
> step is timed; Force Stop waits 10 s (was 6 s) before SIGKILL. **"Start with the car" bootloop guard:** after 3
> autostarts within 15 minutes (cold boots, so only when something resets the unit) the next boot skips the autostart
> (`start with the car: SKIPPED by the bootloop guard`); menu Start, a clean daemon exit or 10 minutes of uptime reset
> it (`/var/opt/aa/bootguard`).

> NBT-AAD is free of charge. Selling it, or bundling it with anything sold, is not permitted.
> If you paid for it, you were scammed. Official releases are published only at
> https://github.com/Sq00ky/Supra-iDrive6-Android-Auto.

---


> **Fork F (exclusivity), experimental:** daemon `v1.8.8x-exclusive`, iDrive app `rhmiapp-9x`. While Android Auto is on
> screen, the iDrive app holds iDrive on its own blank state (hmiState 13), like iDrive's own CarPlay screen, so knob
> turns, pushes, tilts and touches no longer operate the iDrive menu underneath. BACK stays with Android Auto (iDrive
> is put straight back on the blank state). MENU opens Android Auto's own menu (since v1.8.29 / rhmiapp-14; tick
> **MENU key opens iDrive** to make it leave Android Auto like native CarPlay). OPTION / MEDIA / COM / NAV / MAP are
> handled by iDrive itself and take you to iDrive: Android Auto steps aside; the Android Auto entry brings it back.
> Phone calls are the exception: the call is shown in Android Auto, so when iDrive switches to its own call screen
> Android Auto stays up, and the blank state is taken back once the call ends (CDS `communication.callStatus0/1`).
> `NBT_AA_HOLD=0` in `nbt-aad.env` restores the old behaviour. App log lines: `hold: ...`, `CDS communication...`.
> v1.8.8x / rhmiapp-9x add park assist (iDrive gets the screen for the parking view while `sensors.PDCStatus` is 1,
> `NBT_PDC_ACTIVE` / `NBT_PDC_ACTION=0` override), the MINI round-display defaults, and put iDrive back on the blank
> state when OPTIONS takes its focus. App log: `CDS sensors.PDCStatus ...`; daemon log: `[Park] ...`.
> v1.8.40-sppreg: no more Restart needed when the phone is connected over Bluetooth but never opens the Android Auto
> Bluetooth link (no `/dev/btspp0`; seen on a MINI F60 after a cold start and after the head unit slept). Restart
> worked because it registers the Android Auto Bluetooth service again while the phone is connected; the daemon now
> does that by itself (SppUuidDeregister + SppUuidRegister, no restart) every 5 s for the first 2 minutes without a
> link, then every 30 s; the 2 minutes start again after every link, session or access-point return.
> `NBT_SPP_REREG_S=<s>` sets the short interval, `0` turns it off. Log: `[BluetoothManager] ... re-registering the
> Android Auto Bluetooth service`. Not car-verified yet.
>
> v1.8.39-panel: Android Auto uses the whole display on cars whose CarPlay coding is not full screen. The screen size
> came from the CarPlay coding in `carplay.ini`, which is the factory CarPlay *picture*; on cars coded for the smaller
> CarPlay window (left ~2/3, iDrive split screen on the right) Android Auto filled only that part. The daemon now reads
> the display size from LayerManager (`get screen 0`) and uses it when it is larger than the coded picture (this also
> covers the MINI 1180_480 case). New log line: `[Car] display (LayerManager screen 0): ...`. `NBT_SCREEN=<w>x<h>`
> still overrides. Not car-verified yet.
>
> v1.8.38-btconn: the car's Bluetooth audio source is now requested through the ConnectionMaster (`audioconn -b 0xc7`,
> the one call iDrive makes when Bluetooth audio is picked) instead of the AudioManagement focus form `-b 0x3a 0x1`.
> Car-verified on a MINI F60 (3 of 3 sessions): granted at once and music starts by itself, even from FM radio, where
> the focus form was refused for every retry. `NBT_BT_CONN=0` goes back to the focus form. MINI round display
> (`1180_480` coding): touch x offset -20 by default (taps landed ~20-30 px right of the finger; tester-verified with
> `NBT_TOUCH_OX=-20`, which still overrides).
>
> v1.8.37-cpwindow / rhmiapp-19 (MINI tester feedback, not car-verified yet):
> - With the CarPlay hand-off the screen window "1280" is now created only when an Android Auto phone connects. Created
>   at start it took LayerManager surface 1280 away from the factory CarPlay window (same id), so "Start automatically"
>   + an iPhone first after boot gave a black CarPlay, and after a Force Stop the surface stayed without a window until
>   reboot. Until a phone connects, the daemon no longer touches surface 1280 at all (also not on exit).
> - CarPlay taking the screen while an Android Auto session is still open (both phones connected) now gets the
>   decoder's CarPlay settings at once instead of a zoomed picture: while Android Auto is minimized the daemon checks
>   every 2 s whether iDrive made surface 1280 visible. The Android Auto entry brings Android Auto back (decoder
>   rebuilt, phone asked to restart its stream). `NBT_CP_LIVE_RETUNE=0` disables. Log: `CarPlay took the screen...`,
>   `back to Android Auto after CarPlay`.
> - `audioconn` retries after a refused Bluetooth source are spaced out (5, 7, 9, 11, 12, 12 s instead of every 3 s):
>   a tester saw iDrive's watchdog warn about NBTMediaMainApp during the 3 s burst. `NBT_AUDIOCONN_RETRY_S` sets the
>   first pause, `NBT_AUDIOCONN_RETRIES` (now default 6) the count.
> - Menu checkbox renamed **Start automatically** (it starts the daemon when the head unit boots, not with the
>   ignition). Install the new `NbtAad.xml` and `NbtAad_ALL_Texts.zip`.
>
> v1.8.36-wakefix: ignition off/on within ~10 min (the head unit stays up, only its Wi-Fi access point goes off and on)
> left a running daemon that the phone could no longer reach: it took the Wi-Fi details over Bluetooth but never
> connected, until Force Stop + Start (F31 and Supra reports). The daemon now watches the access point and, when it
> comes back or a phone takes the Wi-Fi details without connecting, runs `pfctl -d` again and re-creates its listening
> socket. Log lines: `[Wireless] ... access point is off`, `[Wireless] ...: 'pfctl -d' returned N`,
> `[TcpServer] listener on port 5277 re-created`. Not car-verified yet.
>
> rhmiapp-18: during a burst of hard keys iDrive can refuse to go back to the blank state for a few seconds; the app
> now keeps retrying (up to 8 times, while keys keep coming and 6 s after) instead of giving up after 3.
>
> v1.8.35-keyhold / rhmiapp-17: every key that goes to the phone (also a short BACK) lets the iDrive app put iDrive
> back on the blank state, so a short BACK after MENU no longer minimizes Android Auto (iDrive's BACK went to its main
> menu, which the app took for a hard key). If iDrive returns to the Android Auto screen by itself while Android Auto is
> minimized (BACK in iDrive), Android Auto comes back instead of a blank screen. No re-hold during a call.
>
> v1.8.34-hardkeys / rhmiapp-16: COM / MEDIA / NAV / MAP behave like MENU: the key goes to Android Auto (dialer,
> media, maps) and iDrive is put back on the blank state, so Android Auto stays on screen (before, a quick burst of
> presses could still minimize it). `NBT_KEY_COM=0` (or `_MEDIA` / `_NAV` / `_MAP`) keeps a key for iDrive only. The
> app change only renames a log line; the daemon alone fixes it.
>
> v1.8.32-cphandoff: Android Auto ⇄ CarPlay switching, from the MINI F56 tester's car-verified scripts, now built in
> (NBT_CP_HANDOFF=0 turns it off). The decoder context is created when an Android Auto phone connects, not at start,
> so an iPhone connecting first after boot keeps the factory CarPlay picture. Once Android Auto took the decoder, the
> factory cannot recreate its own for the rest of the boot and CarPlay plays through ours: 2.5 s (v1.8.33) after an Android Auto
> session ends it gets CarPlay's factory settings (AVC1 at the carplay.ini size, no queue, prefetch 0: no zoomed or
> laggy CarPlay), and the next Android Auto connection rebuilds it and reopens mdev (no black screen after CarPlay).
> When Android Auto is shown, LayerManager layer 0x800d0000 (left hidden and offset by iDrive after CarPlay) is made
> visible and full screen. Force Stop still leaves CarPlay black until the next reboot once Android Auto has run.
>
> v1.8.31-ptt (experimental): the daemon logs the steering-wheel voice button (MOST 15/02 fkt 512, read-only copies,
> `[PTT] voice button released after N ms (short|long press)`). With `NBT_PTT_ASSISTANT=1` a long press (750 ms, the
> same as iDrive uses) also starts the Google Assistant (Android Auto key 84). iDrive still sees the button.
>
> v1.8.30-night / rhmiapp-15: Android Auto's night mode follows the car, like native CarPlay. The iDrive app reads the
> CDS property `vehicle.dayNightChangeOver` and the daemon maps it the way the factory CarPlay service does (2 night,
> 1 day, 0 no change, 3 invalid) and updates the phone whenever it changes. `NBT_NIGHT_MODE=0/1` still forces it.
> Update both (no XML change).
>
> v1.8.29-menukey / rhmiapp-14: MENU while Android Auto is on screen opens Android Auto's menu and iDrive is put back
> on the blank state (before, MENU did both: the phone went home and Android Auto minimized). New checkbox **MENU key
> opens iDrive** (`/var/opt/aa/menu-idrive`): MENU then minimizes Android Auto like native CarPlay and is not sent to the
> phone. Update both, plus `NbtAad.xml` and `NbtAad_ALL_Texts.zip`.
>
> v1.8.28-back2500: holding BACK minimizes Android Auto after 2.5 s (was 5 s). `NBT_BACK_HOLD_MS` still overrides it.
>
> rhmiapp-13: BACK and tilt-left reach iDrive too and pop it from the blank Android Auto state to the app's menu, where
> the knob then drove the menu rows under the picture until the next 1 s poll. The app now puts iDrive back on the
> blank state as soon as the menu gets focus.
>
> v1.8.27-touchport: iDrive touch works again while Android Auto is minimized. The daemon and the HMI share the touch
> UDP port 30567 and the network stack delivers each datagram to only one of them (ours), so iDrive got no touch while
> the daemon ran. The daemon now holds the port only while Android Auto is on screen.
>
> v1.8.26-backheld / rhmiapp-12: while BACK is held the daemon tells the iDrive app (state file overlay field 2), and the
> app stops pulling iDrive back to the blank Android Auto state, which fought the BACK-hold minimize. Update both.
>
> v1.8.25-apcfg: the hotspot password now comes from `iap2.cfg`, not `iap2_Wifi.cfg`. Reverse-engineered from
> `Connectivity`: on every AP config update it rewrites `iap2.cfg` when the SSID or password changed, but only
> rewrites `iap2_Wifi.cfg` when the name, channel or version changed. After the car makes a new hotspot password,
> `iap2_Wifi.cfg` keeps the old one (seen on a BMW). `iap2_Wifi.cfg` is still read as a fallback: if the phone takes
> the Wi-Fi details twice without connecting, the other password is offered. `NBT_AP_CFG` still pins one file.
>
> v1.8.24-reassert: coming back to Android Auto from the iDrive menu or after park assist, the picture is raised again
> 2 s and 5 s later (it could come back black).
>
> v1.8.23-usblog / rhmiapp-10: logs are no longer kept in RAM (`/dev/shmem`). New **Save logs to USB** checkbox in the
> Android Auto menu: when ticked, both logs go to the USB stick that is plugged in (found automatically).
> v1.8.22-btplay (Fork F is now the main tree): Bluetooth music start (issue #9). The Bluetooth source request is retried
> for 30 s instead of ~10 s (`NBT_AUDIOCONN_RETRIES`, default 10). Two opt-in experiments for cars where the request is
> granted but music stays silent until Bluetooth audio is picked in iDrive: `NBT_BT_PLAY=1` (send the Bluetooth
> source's SourceActivity PLAY once the car grants it) and `NBT_BT_CONN=0xc7` (request it through the
> ConnectionMaster like the iDrive HMI). Not car-verified yet.

## Contents

Fixes in this folder: [FIXES.md](FIXES.md).

1. [Features](#features)
2. [Requirements](#requirements)
3. [Install](#install)
4. [Using it](#using-it)
5. [Configuration (`nbt-aad.env`)](#configuration-nbt-aadenv)
6. [Logs](#logs)
7. [Troubleshooting](#troubleshooting)
8. [Other cars: controller setup wizard](#other-cars-controller-setup-wizard)
9. [How it works](#how-it-works)
10. [The iDrive app (rhmiapp/)](#the-idrive-app-rhmiapp)
11. [Building and tests](#building-and-tests)
12. [Source layout](#source-layout)
13. [Known issues and next steps](#known-issues-and-next-steps)
14. [License](#license)

---

## Features

All of these are on by default. Anything that has worked in a car becomes the default, so a working install needs
no settings file.

| Feature | Status |
|---|---|
| Wireless connection (Bluetooth -> the car's own Wi-Fi hotspot -> TCP), hosted by the head unit | Car-verified |
| Video: 1280x480 panel, hardware H.264 decode, crop mode, DPI 140 | Car-verified |
| Touch, including two-finger pinch | Car-verified (Supra); 1440x540 units that split touch messages over several datagrams: fixed in v1.8.4, not yet car-verified |
| iDrive controller: rotate, push, tilt and hard keys sent to the phone (Supra / CCS35up) | Car-verified |
| Media audio over the phone's Bluetooth A2DP link (the car switches to its Bluetooth source automatically) | Car-verified |
| Navigation prompts mixed into the Bluetooth audio | Car-verified |
| "Hey Google" / Assistant using the car's microphone (`Carplay_Siri_1mic` speech scenario) | Car-verified |
| Assistant replies audible (the car's Bluetooth source is requested again when the mic closes) | Car-verified |
| Driving status, night mode | Works; night mode follows the car's day/night (like CarPlay) since v1.8.30 / rhmiapp-15 |
| iDrive menu entry with its own icon and a "Connecting..." text | Car-verified |
| Menu: **Start** | Car-verified |
| Menu: **Force Stop**, **Restart** | Fixed in v1.8.3 (state file), not yet car-verified |
| Reverse gear: Android Auto steps aside for the camera and comes back afterwards | Car-verified |
| Phone's "exit" (car) icon returns to iDrive; the menu entry brings Android Auto back | Car-verified |
| BACK key; hold BACK for 2.5 s to minimize Android Auto | Car-verified (short press and hold) |
| Bluetooth link recovery: SPP register retry; skips a `btspp` link that belongs to the BMW/Toyota companion app | Retry car-verified |
| Bluetooth service registered again when no Android Auto link appears (every 5 s for 2 min, then 30 s; replaces pressing Restart) | v1.8.40, not yet car-verified |
| Single instance: starting again brings the running daemon to the front | Works |
| Older CCS controller (E5 fkt C10/C20, instance 01) | Keys and push seen working in G30 / 1180x480 logs; knob rotation fixed in v1.8.4 (message counter), not yet car-confirmed |
| Other panel sizes / car make (read from the car's `carplay.ini`) | Works on 1280x480; others untested |
| Car make on Android Auto's exit icon: Toyota, BMW, MINI, Rolls-Royce (from the car's CarPlay logo, then `Manufacturer`) | v1.8.5, MINI / Rolls-Royce not yet car-verified |
| No black screen over iDrive while the daemon starts (overlay stays hidden until a phone session) | v1.8.5, not yet car-verified |
| Native CarPlay keeps its own picture size after Android Auto (surface 1280 regions handed back on hide) | v1.8.6, not yet car-verified |
| Menu: **Start automatically** checkbox (called "Start with the car" before rhmiapp-19), **About** page | rhmiapp-7, not yet car-verified |

Off by default (opt-in):

| Feature | Status |
|---|---|
| Android Auto turn-by-turn in the **instrument cluster and windscreen HUD** (J29 Supra), with no iDrive route: menu checkbox **Turn arrows in cluster/HUD** ([details](#turn-arrows-in-the-cluster-and-hud-j29)) | The graph the daemon sends was car-verified from a test client on a J29 (cluster + HUD, 2026-10-31); from the daemon itself not yet car-verified |

Not done yet:
- iDrive still reacts to touches while Android Auto is on screen.
- Media over the head unit's own audio path (A2I) instead of Bluetooth is experimental: PCM flows, but nothing is
  audible yet.

## Requirements

- An NBT Evo ID6 head unit, running firmware like the Supra's (built and tested on `NBTevo_T21424I`; a friend's
  `NBTevo_Y24073J` has identical Bluetooth / SPP configuration).
- Root shell access to the head unit, plus a USB stick for copying files and collecting logs.
- One of these coded on the head unit, otherwise HmiMain3D doesn't load PluginApps: **A4A**, **A4A_REMOTE_APPS**,
  **REMOTE_HMI** or **TOUCH_COMMAND**.
- The car's CarPlay Wi-Fi hotspot config (`iap2.cfg` / `iap2_Wifi.cfg`). This is stock on cars that have CarPlay.
- An Android phone with Android Auto and wireless Android Auto enabled, paired with the car over Bluetooth
  **for calls** (Android Auto needs the hands-free link).

## Install

The USB stick mounts as `/mnt/umass<something>`. On the Supra it is `/mnt/umass01200t7`, and another car used
`/mnt/umass01200t12`. Check with `ls /mnt`; with only one stick inserted, `/mnt/umass*` also works. The examples
below use `/mnt/umass01200t7`.

The car shell takes plain one-line commands, so run them one at a time. If a copy fails with "read-only file system",
remount the target writable first, the same way you got the shell.

### 1. Daemon

```
mkdir -p /opt/aa
cp /mnt/umass01200t7/nbt-aad /opt/aa/nbt-aad
chmod 755 /opt/aa/nbt-aad
```

### 2. iDrive app (menu entry)

The `Plugins` folder doesn't exist on a stock head unit, so create it:

```
mkdir -p /opt/hmi/ID5/data/ro/Plugins/NbtAad
cp /mnt/umass01200t7/libNbtAad.so /opt/hmi/ID5/data/ro/Plugins/NbtAad/
cp /mnt/umass01200t7/NbtAad.manifest /opt/hmi/ID5/data/ro/Plugins/NbtAad/
cp /mnt/umass01200t7/NbtAad.xml /opt/hmi/ID5/data/ro/Plugins/NbtAad/
cp /mnt/umass01200t7/NbtAad_ALL_Texts.zip /opt/hmi/ID5/data/ro/Plugins/NbtAad/
cp /mnt/umass01200t7/NbtAad_ALL_Images.zip /opt/hmi/ID5/data/ro/Plugins/NbtAad/
```

### 3. Reboot

Hold the power button for about 30 s. After the restart, **Media/Radio** has an **Android Auto** row after the
Bluetooth audio row.

### 4. Optional: settings file

`/opt/aa/nbt-aad.env` is optional. The only line most people want is a persistent log:

```
NBT_LOG_FILE=/mnt/umass01200t7/nbt-aad.log
```

See [Configuration](#configuration-nbt-aadenv).

### Updating

Never `cp` over a file that is in use; the running daemon or HmiMain3D can crash. Move the old file aside, copy the
new one, then reboot:

```
mv /opt/aa/nbt-aad /opt/aa/nbt-aad.old
cp /mnt/umass01200t7/nbt-aad /opt/aa/nbt-aad
```

Do the same for `libNbtAad.so` (`mv` it to `libNbtAad.so.old` in the plugin folder). Then hold power for 30 s.

### Turning things off / uninstalling

| File / action | Effect |
|---|---|
| `touch /opt/aa/app.disable` | iDrive app does nothing (no entry) |
| `touch /opt/aa/status.disable` | Entry text stays "Android Auto" (no Connecting... text) |
| `touch /opt/aa/spinner.disable` | No connecting animation |
| `touch /opt/aa/gear.disable` | App does not watch the gear (no reverse handling) |
| `touch /opt/aa/mic.disable` | Car microphone stays closed |
| Delete `/opt/hmi/ID5/data/ro/Plugins/NbtAad/` and reboot | Removes the menu entry completely |

## Using it

### First connection

1. Install, reboot, and make sure the phone is paired with the car and connected (calls + audio).
2. Open **Media/Radio -> Android Auto -> Start Android Auto**.
3. The row reads **Android Auto: Connecting...** while the phone gets the Wi-Fi details over Bluetooth and joins the
   car's hotspot. When the session is up, Android Auto fills the screen and iDrive goes back to its main menu
   underneath.

If it stays on Connecting... and nothing happens, see [Troubleshooting](#troubleshooting). Usually the phone needs a
Bluetooth reconnect.

### Menu

| Row | Action |
|---|---|
| Start Android Auto | Starts `/opt/aa/nbt-aad`, or brings a running one to the front. Reads "Connecting..." during setup, then "Open Android Auto" |
| Force Stop | Asks the daemon to quit; SIGTERM after 3 s, SIGKILL after 6 s |
| Restart | Force Stop, then Start |
| Save logs to USB (checkbox) | Off by default: no log files, nothing kept in RAM. When ticked, the daemon and the iDrive app append to `nbt-aad.log` / `nbt-aad-app.log` on the first writable USB stick (`/mnt/umass*`). A stick plugged in later is picked up within 5 s; a file over 64 MB is renamed to `*.old.log`. Stored as `/var/opt/aa/usblog` (a hand-made `/opt/aa/usblog` also counts). `NBT_LOG_FILE` still forces a daemon log file |
| MENU key opens iDrive (checkbox) | Off by default: MENU opens Android Auto's menu (phone home screen) and iDrive stays on the blank state. When ticked, MENU minimizes Android Auto and opens the iDrive main menu, like native CarPlay, and is not sent to the phone. Stored as `/var/opt/aa/menu-idrive` (a hand-made `/opt/aa/menu-idrive` also counts); read on every MENU press |
| Show driving mode changes (checkbox) | Off by default. When ticked, changing the driving mode (Sport, Comfort, ECO PRO, ...) makes Android Auto step aside like in reverse so iDrive's own driving-mode display is visible, then Android Auto comes back by itself after `NBT_DRIVEMODE_SHOW_MS` (default 3500 ms). The iDrive app reads CDS `driving.mode` / `driving.mode35up` and logs every value (`CDS driving.mode = '{"mode":n}'`). Stored as `/var/opt/aa/show-drivemode`, read on every change. Car-tested on a Supra (`driving.mode35up` 3 / 4). v1.8.45 / rhmiapp-22 (car-verified, Supra): the app also sends the previous mode, so the first change after the daemon starts is no longer taken as its baseline; the daemon reacts at once instead of on its next 250 ms tick; the LayerManager switch runs in one shell. iDrive is visible 0.3-0.5 s after the CDS value (`... HIDDEN (Z-Order: -1000, N ms)`: 262-468 ms). That was quick enough to show iDrive's blank Android Auto page before its popup, so v1.8.46 waits `NBT_DRIVEMODE_DELAY_MS` (default 400 ms; `0` = at once) after the change; a further change while iDrive has the screen only extends it |
| Turn arrows in cluster/HUD (checkbox) | Off by default. When ticked, Google Maps' (or another Android Auto nav app's) next turn, its distance and the road name show in the J29's instrument cluster and windscreen HUD, as with an iDrive route. See [Turn arrows in the cluster and HUD](#turn-arrows-in-the-cluster-and-hud-j29). Stored as `/var/opt/aa/kombi-nav` (a hand-made `/opt/aa/kombi-nav` also counts); the daemon checks it once a second, so unticking clears the arrow at once |
| Start automatically (checkbox; "Start with the car" before rhmiapp-19) | When ticked, the iDrive app starts the daemon 15 s after the head unit boots (not on every ignition-on: the head unit stays up ~10 min after ignition off, and the daemon keeps running), so a phone that connects on its own gets Android Auto without opening this menu. Stored as `/var/opt/aa/autostart` on the car's persistence partition, which is writable at runtime (`/opt/aa` usually isn't); a hand-made `/opt/aa/autostart` also counts (needs the status polling, i.e. no `status.disable`) |
| About | One wrapping text block (a Richtext list, like BMW's own ActiveSeating intro text, English only): "NBT-AAD: native wireless Android Auto", "Developed by nanaisu", the GitHub address, and the free-of-charge notice |

While Android Auto is on screen, Force Stop and Restart are ignored. Knob presses can leak to the iDrive menu
underneath the overlay, so this stops a stray press from killing the session.

### Getting in and out

| Action | Result |
|---|---|
| Phone's car / "exit" icon | iDrive gets the screen; the session keeps running in the background |
| Media/Radio -> Android Auto -> Start (while running) | Android Auto comes back |
| Reverse gear | Camera shows; Android Auto returns when you leave reverse (unless you had exited it) |
| Manual gearbox: reverse, then neutral + handbrake | Android Auto returns after 5 s in neutral (`NBT_NEUTRAL_RESUME_S`, `0` = off) (park assist would otherwise keep iDrive's parking view up) |
| BACK (short press) | Back in Android Auto |
| BACK held 2.5 s | Minimizes Android Auto like the exit icon (`NBT_BACK_HOLD_MS`) |
| Phone out of range / Wi-Fi drops | After 2 s without data iDrive gets the screen back |

### Controller (Supra defaults)

| Control | Android Auto |
|---|---|
| Rotate | Scroll / move focus |
| Push | Select |
| Tilt up/down/left/right | D-pad |
| MENU | Home (Android Auto's menu); with **MENU key opens iDrive** ticked: minimizes Android Auto |
| BACK | Back |
| COM | Call |
| MEDIA | Media |
| NAV, MAP | Navigation |
| OPTION | Nothing (`NBT_KEY_OPTIONS`) |

### Turn arrows in the cluster and HUD (J29)

With **Turn arrows in cluster/HUD** ticked, the daemon turns Android Auto's navigation status (channel 10: next
maneuver, distance, road) into the KOMBI's own route-guidance graph and sends it from the head unit straight to the
cluster, so the arrow, distance and street show in the cluster and the windscreen HUD without an iDrive route.

| | |
|---|---|
| Cars | J29 Supra (the KOMBI's guidance service, SOME/IP `0xB006` at `160.48.199.96:30502`). On any other car the KOMBI refuses the first frame or isn't there, and the feature turns itself off for the session (log: `[KombiNav] the KOMBI rejected a frame ...` / `no KOMBI at ...`) |
| When the arrow shows | While the phone guides, once the next maneuver is within 1 mile (`NBT_KOMBI_NAV_APPROACH_MI`, `0` = at once), like a production nav HUD. Distance as `0.5 mi` / `450 ft` (metric distances are shown in mi / ft) |
| When it clears | Guidance off for 8 s (rerouting keeps it), the phone disconnects, the box is unticked, the daemon stops (Force Stop, Restart), or the daemon crashes (cleared from the crash handler). A daemon killed with SIGKILL leaves `/dev/shmem/nbt-aad-kombi-nav.shown`; the next start sends the clear first |
| iDrive navigation | Wins: when an iDrive route starts (seen in the system log, `sloginfo -w`), the daemon stops writing without clearing, and takes over again at the next Android Auto route. Without `sloginfo` the log says so; then don't start an iDrive route while Android Auto guides |
| Safety | Only route-guidance model updates are sent (never create / dispose), one connection, our own SOME/IP client id; a frame the KOMBI rejects stops the feature until the next session |
| Turning it off | Untick the box (the arrow clears within a second) or `NBT_KOMBI_NAV=0` in `nbt-aad.env` |

Not covered: lane guidance, the "then" preview, the cluster's small map.

### Tips

- **Assistant replies are quiet next to music.** During a reply, Android switches to a call-like Assistant mode and
  scales the reply by the phone's **Call volume** for the car, not its Media volume. With the phone connected to the
  car, raise Call volume (Settings -> Sound & vibration -> Volume, or the volume panel's settings). It is stored per
  Bluetooth device, so you only need to do it once.

## Configuration (`nbt-aad.env`)

`/opt/aa/nbt-aad.env` holds one `NAME=value` per line. Lines starting with `#` and blank lines are skipped.

Limits and behaviour:
- **Only the first 15 settings are used, and each line is limited to 159 characters.**
- The file is read when the iDrive menu starts the daemon. A daemon started from a shell doesn't read it; put the
  settings in front of the command instead, e.g. `NBT_LOG_FILE=/mnt/umass01200t7/a.log /opt/aa/nbt-aad`.
- Changes take effect at the next **Start** (or Restart). No reboot is needed.

On/off settings accept `1`/`0`; most also accept `on`/`off`, `yes`/`no` and `true`/`false`.

### Common settings

| Setting | Default | Effect |
|---|---|---|
| `NBT_LOG_FILE` | (none) | Append the daemon log to this file; it survives a head-unit reset. Same name = appended |
| `NBT_MENU_RETURN` | home | Where iDrive goes once connected: `media` = Media/Radio list, `off` = stay in our menu |
| `NBT_RX_TIMEOUT_S` | 2 | Seconds without data from the phone before iDrive gets the screen back; `0` = off |
| `NBT_BACK_HOLD_MS` | 2500 | BACK hold time to minimize; `0` = off (BACK forwarded at once) |
| `NBT_KNOB_INVERT` | off | `1` reverses the rotary direction |
| `NBT_KEY_<SLOT>` | see above | Android keycode for a hard key, `0` = ignore. SLOT = `MENU BACK OPTIONS COM MEDIA NAV MAP`. Defaults 3 HOME, 4 BACK, 0, 5 CALL, 65537 MEDIA, 65538 NAV, 65538 NAV |
| `NBT_KNOB_INST` | 03 | CCS35up controller instance (hex) |
| `NBT_KNOB_CCS_INST` | 01 | Older CCS controller instance (hex) |
| `NBT_VEHICLE` | from `carplay.ini` (CarPlay logo, then Manufacturer) | `toyota`, `bmw`, `mini` or `rollsroyce`: the car make Android Auto shows (exit icon) |
| `NBT_SCREEN` | from `carplay.ini` (a `1180_480` coding counts as a 1280x480 panel) | Panel size, e.g. `1440x540` (BMW 10.25") |

### Features you can turn off (all on by default)

| Setting | Turn off with | What it does |
|---|---|---|
| `NBT_WIRELESS` | `0` | HU-hosted wireless. Off = old mode where the daemon scans for a phone running "Start head unit server" |
| `NBT_AUDIO_BT` | `0` | Only read when `NBT_AUDIO_MODE` is unset. Since v1.8.61 the default is browser audio (see `NBT_AUDIO_MODE`); `1` = media over Bluetooth A2DP, `0` = experimental A2I pipeline (see below) |
| `NBT_GUIDANCE_BT` | `0` | Bluetooth mode only: nav prompts mixed into the Bluetooth audio (sink declared as system audio) |
| `NBT_VOICE_SCENARIO` | `0` | Car mic routed to the Assistant via the `Carplay_Siri_1mic` speech scenario. Without it the Assistant hears silence |
| `NBT_BT_REFOCUS` | `0` | Request the car's Bluetooth source again after the Assistant closes the mic, so the reply is heard |
| `NBT_MIC` | `0` | Car microphone (also `/opt/aa/mic.disable`) |
| `NBT_KNOB` | `0` | iDrive controller to the phone |
| `NBT_TOUCH_FWD` | `0` | Touch to the phone |
| `NBT_HIDE_IDLE` | `0` | iDrive gets the screen while no phone is connected. Off = overlay stays up for the whole run |
| `NBT_GEAR_ACTION` | `0` | Reverse-gear handling |
| `NBT_CTL` | `0` | Single-instance control port (UDP 127.0.0.1:52790) |
| `NBT_KEEP_PF` | set to `1` | Don't run `pfctl -d` at start (see [Troubleshooting](#troubleshooting)) |

### Advanced / experimental

Leave these unset unless you are testing.

| Setting | Effect |
|---|---|
| `NBT_AA_PORT` | TCP port the phone is told to use (default 5277). Port 52770 has been tried and failed on one car; under investigation |
| `NBT_BTSPP_DEV` | Use only this SPP node (default: scan `/dev/btspp0..7`) |
| `NBT_WPP_MODE=0\|1\|2` | Pin the Bluetooth handshake mode (default rotates 2, 0, 1 on failures) |
| `NBT_AP_CFG`, `NBT_AP_IF`, `NBT_AP_IP`, `NBT_AP_BSSID` | Hotspot config path / interface / address / BSSID (default: `iap2.cfg`, `iap2_Wifi.cfg` as fallback; `uap0`, 172.16.224.1) |
| `NBT_SPP_UUID_CON` | Connect UUID registered with the car's SPP service (default = AA UUID) |
| `NBT_SPP_REREG_S` | While no Android Auto Bluetooth link is open, register the service again every this many seconds for 2 minutes, then every 30 s (default 5; `0` = off) |
| `NBT_BT_TRACE` | `1` = read-only Bluetooth diagnostics: log, with uptime, when the phone connects to / drops from the car over Bluetooth and which services it has (`DeviceListChanged: phone ... is connected`), to see why the phone does not open the Android Auto link after the car sleeps and wakes. Off by default, not car-verified |
| `NBT_VOICE_SCENARIO_NAME` | Speech scenario name, e.g. `Carplay_Siri_1mic_RefCh` |
| `NBT_BT_SRC`, `NBT_BT_SINK`, `NBT_BT_RELEASE=1` | Audio source/sink for Bluetooth mode (default `0x3a` / `0x1`); release the source when the phone leaves |
| `NBT_AUDIOCONN`, `NBT_AUDIOCONN_SUFFIX`, `NBT_AUDIOCONN_RETRIES` | `audioconn` path (default `/fs/sda0/opt/mm/bin/audioconn`), extra args, retries while the car refuses the source (default 6, max 20), pauses `NBT_AUDIOCONN_RETRY_S` (default 5) s growing by 2 s up to 12 s |
| `NBT_BT_ROUTE_DELAY_MS` | wait before the Bluetooth-source `audioconn` after a session starts (default 1500 ms, 0 = at once). Since v1.8.41 the request is single-flight, at most one `audioconn` runs at a time, and a session that ends cancels its pending request and retries (a phone reconnecting every few seconds used to stack `audioconn` processes until the head unit ran out of memory and rebooted) |
| `NBT_CHILD_NO_PRELOAD` | `1` removes `LD_PRELOAD` (inherited from the HMI) for every child process the daemon starts (`audioconn`, `LayerManagerControl`, `pfctl`, `arp`, `ifconfig`), so they stop loading the graphics libraries (`ldd:FATAL: Could not preload library libEGL_X.gpp.so`, `unknown symbol: kdMain`). Experiment, not car-verified; the start-up line `[Main] LD_PRELOAD=...` shows what is inherited |
| `NBT_TOUCH_CONNECT`, `NBT_TOUCH_PEER` | On by default since v1.8.47 (`NBT_TOUCH_CONNECT=0` turns it off; car-verified on a G11: gestures work under Android Auto): while Android Auto is on screen the touch socket is connected to the touch controller (the first touch PDU's sender, or `NBT_TOUCH_PEER=ip:port`), so BMW Gesture Control PDUs on the same UDP 30567 port go to iDrive instead of being swallowed: circle clockwise / anticlockwise = volume up / down, two fingers = next track or mute (as set in iDrive). Swipe right and point only close / confirm iDrive popups, so over Android Auto they normally do nothing (iDrive does not even enable them in the camera unless a popup is up); swipe left is ignored by iDrive. The learned address is kept in `/var/opt/aa/touch-peer`, so after the first touch every later start connects at once. Car-confirmed (G11, v1.8.43): the camera sends from `160.48.199.16:30501`, ~30 PDUs/s of `0xB5308006` hand coordinates plus `0xB5308005` gestures. Gestures that still reach the daemon are logged decoded (`[TouchFwd] gesture camera: rotate clockwise (volume up) (id 3)`). If Android Auto touch stops working, set `NBT_TOUCH_CONNECT=0` and delete `/var/opt/aa/touch-peer` |
| `NBT_BT_PLAY` | **Experiment, off.** `1` = once the car grants the Bluetooth source, wait `NBT_BT_PLAY_DELAY` ms (1500) and send SourceActivity PLAY to it (`AuxInBTAudio.DMMAudioSource` request 2), like picking Bluetooth audio in iDrive; `2` = also AVRCP START (`BlueMedia.SGMedia`). Log: `[BtPlay] ...`. Not car-verified |
| `NBT_BT_CONN` | ConnId of the car's Bluetooth source, requested through the ConnectionMaster (`audioconn -b <id>`, the iDrive HMI's call). Default `0xc7` (`audioconn -l`: `eSRC_A2DP ... AuxInBTAudio srcNr: 0x1`), car-verified on a MINI F60 since v1.8.38; `0` = the old focus form `-b 0x3a 0x1` (`NBT_BT_SRC` / `NBT_BT_SINK`) |
| `NBT_SDR_DROP` | AA channels left out of service discovery (default `9` in Bluetooth mode) |
| `NBT_AUDIO_MODE` | `browser` (default since v1.8.61, car-verified), `bt` or `a2i`. `browser`: Android Auto sound comes over the Android Auto connection instead of Bluetooth and plays through the car's browser audio source (see [Browser audio mode](#browser-audio-mode)). `bt`: media, prompts and Assistant stay on Bluetooth A2DP. Unset follows `NBT_AUDIO_BT` if that is set (`1` = `bt`, `0` = `a2i`) |
| `NBT_BROWSER_CONN`, `NBT_BROWSER_IDLE_S`, `NBT_BROWSER_PROMPT_IDLE_MS`, `NBT_BROWSER_REFOCUS`, `NBT_BROWSER_PCM`, `NBT_BROWSER_RATE`, `NBT_BROWSER_FRAG`, `NBT_BROWSER_FRAGS`, `NBT_BROWSER_ASOUND` | Browser mode: connection id (default `0xb8`, `0` = don't route), release the source after N s without sound once AA media has played (default `10`, `0` = keep it until the session ends), release it N ms after a prompt or system sound when no AA media is playing (default `1000`, `0` = use the 10 s rule), ask for it again when the mic closes (default on), `card:device` instead of the preferred device, device rate (default `48000`; this car's browser device takes 22050..44100, so 44100 is used), fragment size in bytes (default `4096`, rounded down to a power of two: A2I refuses any other size), fragment count (default `5`; if the device refuses, the daemon tries twice the count, then 8 fragments of twice the size), shim path (default `/opt/mm/lib/libasound/libasound.so`) |
| `NBT_DUCK_DB`, `NBT_DUCK_ATTACK_MS`, `NBT_DUCK_HOLD_MS`, `NBT_DUCK_RELEASE_MS`, `NBT_MEDIA_GAIN_DB`, `NBT_GUIDANCE_GAIN_DB`, `NBT_SYSTEM_GAIN_DB` | Browser mode ducking: media level under a prompt or system sound (default `-12` dB), ramp down / hold / ramp up (default `80` / `600` / `500` ms), fixed per-stream gains (default `0` dB) |
| `NBT_SYSTEM_SINK` | `1` / `0` forces the SYSTEM_AUDIO sink (ch 16) in / out of service discovery (default: in browser mode only) |
| `NBT_SPEECH=1`, `NBT_SPEECH_EP`, `NBT_SPEECH_SRC`, `NBT_SPEECH_SINK` | Play AA speech (ch 8) on an A2I endpoint. Off, test parked only |
| `NBT_AUDIO_STAGE`, `NBT_MEDIA_RATE`, `NBT_ROUTE_MODE`, `NBT_ROUTE_MEDIA`, `NBT_ROUTE_NAV`, `NBT_ROUTE_SINK` | A2I pipeline (`NBT_AUDIO_BT=0` only). Stages 0-2 are car-stable but not audible yet. Test parked only |
| `NBT_MIC_DUMP=1` | Dump captured mic PCM |
| `NBT_VID_INSETS=top,bottom,left,right\|off` | Phone UI insets (default `0,240,0,0` on 1280x480; MINI round display with the `1180_480` coding: `0,240,40,40`, car-verified on a MINI F60) |
| `NBT_TOUCH_OX` / `NBT_TOUCH_OY` | Touch offset in canvas pixels (default 0; MINI `1180_480` round display: x -20, tester-verified on a MINI F60) |
| `NBT_VID_DPI` | Video DPI (default 140, MINI `1180_480` round display 152; `0` = phone default) |
| `NBT_VID_RES`, `NBT_VID_MW`, `NBT_VID_MH` | Other video config overrides |
| `NBT_VID_SRC_H`, `NBT_VID_DST_H` | Decoder / renderer heights (default 720) |
| `NBT_VIDEO_DUMP=1` | Raw H.264 to `/tmp/aa_video.h264` (4 MB cap) |
| `NBT_MMR_WAIT_S` | Wait for the MM-Renderer at start (default 90) |
| `NBT_PURGE_ALL=1` | Destroy every MM-Renderer context, not only `aadvideoctx*`/`cpvideoctx*` |
| `NBT_TOUCH_SX/SY/OX/OY`, `NBT_TOUCH_W/H`, `NBT_TOUCH_IDLE_MS`, `NBT_TOUCH_CFG_H`, `NBT_TOUCH_PORT` | Touch scale/offset, clamp (1280x720), stuck-finger release (400 ms), declared height (720), UDP port (30567) |
| `NBT_TOUCH_PROBE=1`, `NBT_TOUCH_PROBE_SECS` | Log raw touch frames only |
| `NBT_KNOB_RAW=1` | Log every raw controller message |
| `NBT_GEAR_REVERSE` | Gear value for R (default 2; car codes 3 P, 1 N, 2 R, 5 D) |
| `NBT_NEUTRAL_RESUME_S`, `NBT_GEAR_NEUTRAL` | Manual gearbox: seconds in neutral straight after reverse before Android Auto comes back over the park-assist view (default `5`; `0` = off). Leaving neutral or going back into reverse cancels it; not after you exited Android Auto. Gear value for N (default 1). Log: `[Gear] ... reverse -> neutral`. Not car-verified |
| `NBT_REVERSE_FOCUS=1`, `NBT_BACK_HOLD_FOCUS=1` | Also tell the phone it lost the screen in reverse / on BACK hold |
| `NBT_CP_HANDOFF=0` | Turns off the CarPlay hand-off (screen window and decoder created at start and never retuned for CarPlay, as before v1.8.32) |
| `NBT_CP_LIVE_RETUNE=0` | Do not give the decoder CarPlay's settings when CarPlay takes the screen during a live Android Auto session (v1.8.37) |
| `NBT_PTT_ASSISTANT=1` | Experimental: long press of the steering-wheel voice button starts the Google Assistant. Off by default (the button is only logged) |
| `NBT_NIGHT_MODE=0/1`, `NBT_DRIVING_STATUS=0` | Sensor values. Night mode follows the car by default (`vehicle.dayNightChangeOver` via the iDrive app); `NBT_NIGHT_MODE` forces day (0) or night (1) |
| `NBT_CAR_MAKE/MODEL/YEAR`, `NBT_VEHICLE_ID`, `NBT_HU_MAKE/MODEL`, `NBT_SW_VERSION`, `NBT_BT_ADDR` | Single identity fields |
| `NBT_CARPLAY_INI` | Read a different `carplay.ini` (never the factory template in `/opt/conn/data/carplay/`) |
| `NBT_CTL_PORT` | Control port (default 52790) |
| `NBT_KOMBI_NAV` | `1` / `0` forces the cluster/HUD arrows on / off, whatever the menu checkbox says (default: the checkbox) |
| `NBT_KOMBI_NAV_APPROACH_MI` | Show a maneuver only within this many miles (default `1`, `0.1`..`9.9`; `0` = at once) |
| `NBT_KOMBI_NAV_SHOW_BAR=1` | Keep the cluster's red distance bar (hidden by default: without an iDrive route its percentage means nothing) |
| `NBT_KOMBI_NAV_YIELD=0` | Don't watch the system log for iDrive routes |
| `NBT_KOMBI_NAV_HANDLE`, `NBT_KOMBI_NAV_HOST`, `NBT_KOMBI_NAV_PORT` | KOMBI guidance instance handle (default `0xC8`, the same on every boot seen), address (`160.48.199.96`) and port (`30502`) |
| `NBT_DIAG=1`, `NBT_VERBOSE` | Window/LayerManager dump 8 s into a session; verbose protocol log |

Old "squashed" picture mode: `NBT_VID_INSETS=off`, `NBT_VID_SRC_H=480`, `NBT_VID_DST_H=480`, `NBT_TOUCH_SY=1.5`.

### Browser audio mode

The default since v1.8.61 (car-verified). `NBT_AUDIO_MODE=bt` in `nbt-aad.env` goes back to Bluetooth mode.

- The phone gets three audio sinks: media (ch 9, 48 kHz stereo), guidance (ch 8, 16 kHz mono: nav prompts, Assistant
  speech, message read-outs) and a new system-audio sink (ch 16, 16 kHz mono: notification chimes, UI sounds). Media
  then no longer goes to Bluetooth A2DP.
- The daemon mixes the three streams and ducks the media under prompts and system sounds (`NBT_DUCK_*`).
- It plays the mix as one more client of the car's browser audio (`/opt/mm/lib/libasound/libasound.so` ->
  `dev-libasound` -> A2I `input_stereo_3`) and asks for the browser audio source with `audioconn -b 0xb8`. The
  CarPlay sources can't be used: they refuse to allocate unless an iPhone is connected.
- `0xb8` is an *interrupting* source (like the Owner's Manual audio): while it is built the radio or Bluetooth source is
  paused and iDrive's Media menu still shows it. The daemon builds it when Android Auto first makes a sound. Once AA
  media has played it is kept through pauses and released after `NBT_BROWSER_IDLE_S` (10) s of silence; when only a
  prompt or a system sound played (you are listening to the radio) it is released `NBT_BROWSER_PROMPT_IDLE_MS` (1000)
  ms after it, so the radio is cut only for the prompt. Audio that arrives before the car grants the source waits, so
  the start of a prompt is not cut off.
- Calls are unchanged (Bluetooth hands-free). Whether `0xb8` comes back by itself after a call is not known yet.

What to look for in the log: `[Main] audio mode: browser`, `[SDR] added channel 16`, `[Browser] PCM open: card ..`,
`[Browser] device setup: ..`, `[Browser] connection 0xb8 granted`, `[Browser] media ducked under a prompt ..`, and the
10 s `[Browser] blocks ..` statistics line (queued / dropped bytes per stream, route state, write errors).
Handy checks from the shell: `audioconn -l` (is `0xb8` listed as `BrowserFront`?) and `sloginfo | grep -i preAllocate`.

## Logs

| Log | Where |
|---|---|
| Daemon | `<stick>/nbt-aad.log` with **Save logs to USB** ticked in the Android Auto menu (since v1.8.23 nothing is logged otherwise, and nothing goes to RAM) |
| Daemon, fixed file | `NBT_LOG_FILE=/mnt/umass01200t7/nbt-aad-NN.log` in the env file (overrides the checkbox) |
| iDrive app | `<stick>/nbt-aad-app.log` with **Save logs to USB** ticked; always in slog (`slog2info \| grep NbtAadApp`) |
| Daemon state for the app | `/dev/shmem/nbt-aad-state`: `idle\|connecting\|connected <pid> <overlay>` |
| Gear seen by the app | `/dev/shmem/nbt-aad-gear` |
| System log | `slog2info > /mnt/umass01200t7/slog.txt` (no Bluetooth stack messages; those aren't in slog) |

Logs are synced to the stick every 2 s, so they survive a head-unit reset. Pull the stick only with the car off or
after unticking the checkbox.

Useful daemon log lines:
- `Build: v1.8.5-...`: which binary is running.
- `[Voice] car mic scenario: ...`: shows whether the voice scenario was applied.
- `SppUuidRegister` / `GapErrorCode`: Bluetooth registration (0 = OK; 5 then a re-register is the handled retry).
- `[Wireless] opened /dev/btsppN`: the phone opened the Android Auto Bluetooth link.
- `[Wireless] ... companion app (BCL), not Android Auto` / `releasing /dev/btspp0`: a MyBMW / Supra app link was
  skipped.
- `'pfctl -d' ... returned N`: the firewall step (0 = OK).

## Troubleshooting

### Stuck on "Connecting...", no `/dev/btspp` node

The phone isn't opening the Android Auto Bluetooth link. Usually it remembers the car's Bluetooth services from
before nbt-aad was running, so it doesn't know the car offers Android Auto. Work down this list:

1. With nbt-aad running (Start pressed), **disconnect and reconnect the phone over Bluetooth**. This fixed a live
   case in another car.
2. Make sure the phone is connected for **calls**, not only audio.
3. Force-stop MyBMW / Toyota Supra Connect / BMW Connected / AAIdrive. These apps can hold the car's serial link.
4. On the phone, check that wireless Android Auto is on, and that the car isn't under Android Auto -> Connected cars
   -> rejected cars.
5. Forget the car on the phone and pair again, with nbt-aad already running.

Since v1.8.40 the daemon registers its Bluetooth service again by itself when no link appears (what Restart used to
fix): look for `re-registering the Android Auto Bluetooth service` and whether `opened /dev/btspp0` follows within a
few seconds. If it never does, send the log with the time the phone was connected.

While it's waiting, check whether a node appeared: `ls /dev | grep -i spp > /mnt/umass01200t7/spp.txt`. Nodes only
exist while the phone has the link open, so a listing taken after Force Stop proves nothing.

### "out of sync ... dropping byte" floods, or a link that is not Android Auto

`/dev/btspp0` is the BMW/Toyota companion app (BCL/Etch traffic). Since v1.7.8 the daemon recognises it, releases
it, and keeps scanning `btspp1..7`. If the companion link is the only one, the phone hasn't opened Android Auto;
follow the list above.

### Phone joins the Wi-Fi but no picture

The phone connects to TCP 5277 on `uap0` (172.16.224.1). The car's packet filter blocks that port, so the daemon
runs `pfctl -d` at start. The log shows `'pfctl -d' ... returned 0` when it worked. If the return value isn't 0
(one car returned 256), post the log. A single allow rule for the port, instead of disabling the filter, is being
worked on.

### Assistant doesn't hear you / reply is silent or quiet

- Silent mic: the log must show `[Voice] car mic scenario:` with the scenario accepted, and `[Mic] ... on`. Check
  that you haven't set `NBT_VOICE_SCENARIO=0`, `NBT_MIC=0` or `/opt/aa/mic.disable`.
- Quiet reply: raise the phone's **Call volume** while connected (see [Tips](#tips)).

### No music

The car should switch to its Bluetooth audio source on connect (`audioconn` in the log). If it doesn't, selecting
Bluetooth audio in Media/Radio by hand is a workaround.

- Since v1.8.38 the daemon asks the car's ConnectionMaster for connection 0xc7 (`audioconn -b 0xc7`, like picking
  Bluetooth audio in iDrive); the log shows `CMCommand successfully executed`. `CMCommand error in execution` = the
  car refused; the daemon retries for about a minute. Post the log if it ends with `request still failing`.
- If `audioconn -l` on your car lists eSRC_A2DP under another ConnId, set `NBT_BT_CONN=<that id>`. `NBT_BT_CONN=0`
  goes back to the older focus form (`AMCommand ...` lines); with that form a granted source can stay silent until
  Bluetooth audio is picked in iDrive (`NBT_BT_PLAY=1` / `2` are experiments for that case).

### Menu entry missing

- Check that one of A4A / A4A_REMOTE_APPS / REMOTE_HMI / TOUCH_COMMAND is coded.
- Check that the plugin folder is `/opt/hmi/ID5/data/ro/Plugins/NbtAad/` with all five files, and that
  `/opt/aa/app.disable` doesn't exist.
- The app log shows whether the app loaded: `slog2info | grep NbtAadApp`, or `<stick>/nbt-aad-app.log` with Save
  logs to USB ticked (or `touch /var/opt/aa/usblog` from a shell if the menu isn't there).

### Knob turns the wrong way / keys don't work on another car

Run the [controller setup wizard](#other-cars-controller-setup-wizard).

### Native CarPlay zoomed in after using Android Auto

Native CarPlay and the daemon share the LayerManager surface `1280` (carplay.ini `WindowName=1280`). Up to v1.8.5 the
daemon left that surface at its own size (e.g. `NBT_SCREEN=1280x480`) when Android Auto closed, so on a car coded
`1180_480` CarPlay came back zoomed in until a reboot. Since v1.8.6 the daemon reads the factory geometry at start
(log: `Factory CarPlay geometry of surface 1280 (...)`) and puts it back every time Android Auto hides. If CarPlay is
Force Stop, `slay nbt-aad` and other SIGTERM/SIGINT stops go through the same clean shutdown. If the daemon is
killed with SIGKILL (Force Stop's last resort, `slay -9`) or crashes, the iDrive app notices the dead pid in
`/dev/shmem/nbt-aad-state` within a second and restores the screen itself (log: `gone without a clean exit`); the next
daemon start also puts the factory geometry back. If CarPlay is
still zoomed, post the log and the output of `/opt/sys/bin/LayerManagerControl get surface 1280` taken while CarPlay is
on screen.

### Reporting a problem

Send:
- `nbt-aad.log` and `nbt-aad-app.log` from the USB stick (tick **Save logs to USB** first);
- `/opt/sys/etc/nbt_version.txt`;
- what you did and what you saw, with times.

## Other cars: controller setup wizard

`test_controls` is a stand-alone, observe-only controller monitor (built by `make all`). It reads controller messages
as a MOST client and never sends anything to the car.

Run it on a terminal you can read (don't redirect the output), with nbt-aad stopped:

```
./test_controls -w -o /mnt/umass01200t7/controls.env -l /mnt/umass01200t7/controls-raw.log
```

It prompts for each control in turn, 15 s each; wait to skip a key the car doesn't have:
1. knob clockwise 3 clicks;
2. centre push;
3. tilt up, then tilt right;
4. then MENU, BACK, OPTION, COM, MEDIA, NAV and MAP.

iDrive reacts to the presses as usual. At the end it prints:
- the knob instance and direction;
- which slot each key reported;
- notes, e.g. tilt orientation or a key pressed twice;
- the lines to add to `/opt/aa/nbt-aad.env`. Only `NBT_KNOB_INST`, `NBT_KNOB_CCS_INST`, `NBT_KNOB_INVERT` and
  `NBT_KEY_<slot>` values that differ from the defaults are listed; the Supra layout prints "nothing".

`-l` writes a labelled raw log in which every E5 message is tagged with its prompt. Other modes:
`./test_controls -t 120 -r > /tmp/c.log` (`-t N` seconds, `-k HEX,HEX` own FBlock key list, `-r` raw DSI events).

Supported controller protocols (both are decoded at the same time, as the stock HMI does):

| Protocol | Messages | Default instance | Status |
|---|---|---|---|
| CCS35up | E5 fkt 0xC46 knob/push/tilt, 0xC47/0xC4F hard keys | 03 (`NBT_KNOB_INST`) | Car-verified (Supra) |
| CCS (older) | E5 fkt 0xC10 knob, 0xC20 one key event per message | 01 (`NBT_KNOB_CCS_INST`) | v1.8.2, layout from HmiMain3D, not car-confirmed |

CCS hard keys use the same slots as CCS35up (sensor bit n = slot n), so `NBT_KEY_<slot>` overrides apply to both.
nbt-aad logs the first 40 C10/C20 messages raw (`[Knob] ... (CCS protocol)`).

---

## How it works

1. **Bluetooth (`bluetooth_manager`, `dsi_client`).** Over the colibry DSI client, the daemon registers an SPP
   service named `AndroidAuto` with the Android Auto UUID `4de17a00-52cb-11e6-bdf4-0800200c9a66` at the car's
   Bluetooth service (`BSS_GAP.SGBluetoothGap` 35.0). If registration fails, it deregisters and registers once more.
   While no phone opens the link, it deregisters and registers again every 5 s for 2 minutes, then every 30 s
   (`spp_rereg.hpp`, `NBT_SPP_REREG_S`), because a phone that is already connected only opens it after that.
2. **Wireless setup (`wireless_setup`).** When the phone opens the link, Connectivity creates `/dev/btsppN`. The
   daemon scans `btspp0..7`. Links that turn out to be BMW BCL/Etch (the BCL startup magic `12 34 56 78`, `0xDEADBEEF`, `0x13371337`,
   or self-consistent BCL frames) are skipped. A link that closes without a single Android Auto reply (a working
   phone answers WifiStartRequest with `00 02 00 07 ...`) is logged as `never sent an Android Auto reply`: it was
   most likely another app's link (v1.8.48).
   - **MyBMW / BMW Connected / AAIdrive (BMW/MINI with BMW Apps).** AAIdrive has no Bluetooth link of its own: its
     apps reach the car through the MyBMW / BMW Connected BCL tunnel, which the car serves in NBTMediaMainApp's
     A4AHandler on the hard-coded `/dev/btspp0` (it waits for the BCL startup magic `12 34 56 78`). Android Auto setup
     messages written into that link break it (the BMW app drops, AAIdrive loses the car). Since v1.8.48, on BMW/MINI:
     - each new link is listened to for 300 ms before anything is written; a link that speaks BCL first is left alone
       (`NBT_SPP_PEEK_MS`, 0 = off);
     - a node whose links end twice in a row without a single Android Auto reply is left alone for 60 s, or until it
       disappears (`NBT_SPP_SILENT_STRIKES`, 0 = off; hold time `NBT_SPP_SILENT_HOLD_S`).
     Toyota defaults: both off. Not car-verified.
   - **Direction check (v1.8.49, the proper fix).** An Android Auto link is always opened *by the phone* (the AA app
     connects to our UUID from the car's SDP records), and the Bluetooth stack announces it with a ServiceRequest
     first. The BMW Apps link is opened *by the car* (Connectivity's own ServiceConnect to the phone's Serial Port
     service, where MyBMW / BMW Connected listens), with no ServiceRequest. nbt-aad now watches those ServiceRequests
     (read-only) and logs for every new `/dev/btsppN` whether the phone or the car opened it. As soon as Android Auto
     has answered on a phone-opened link once, car-opened links are never read or written again: the car's own BMW
     Apps handler keeps them, so MyBMW and AAIdrive are not disturbed at all. The learned result is kept in
     `/var/opt/aa/spp-model` (`phone-opens-aa`); if Android Auto ever answers on a car-opened link the check switches
     itself off (`mixed`). Until it has learned, the 300 ms listen and the silent-link hold above apply.
     `NBT_SPP_DIRECTION`: 0 off, 1 always on (no learning), 2 learn first (BMW/MINI default; Toyota 0).
     Not car-verified. On the Android Auto link it sends WifiStartRequest plus the hotspot details from the CarPlay
   config `iap2_Wifi.cfg` (e.g. SSID `Toyota51807 CarPlay`, `uap0` 172.16.224.1). The phone joins the hotspot and
   connects to TCP 5277.
   - The btspp device is message-oriented: a short `read()` discards the rest of a message. The reader therefore
     always reads into a large buffer and frames messages itself.
   - The link stays open and serviced for the whole session.
   - Handshake modes rotate on failure: 2 proactive, 0 legacy, 1 version-first.
3. **Session (`aa_session`, `aa_cryptor`, `tcp_server`).** The head unit is the TLS 1.2 client. Service discovery
   comes from `sdr_builder` (Toyota or BMW identity). Channels:
   - 0 control;
   - 1 sensors;
   - 3/5/7 input;
   - 6 video;
   - 8 speech/guidance;
   - 9 media (left out in Bluetooth mode, so the phone uses A2DP);
   - 12 media metadata;
   - 14 mic.

   Speech (8) and mic (14) must stay advertised; without them the phone ends the session.
4. **Audio.** In Bluetooth mode the daemon selects the car's Bluetooth source (`audioconn -b 0x3a 0x1`). Nav
   prompts arrive in the A2DP stream because ch 8 is declared as system audio. For the mic, while the phone has it
   open, the daemon asks `Nuance_ATP_Audio_Service.DSCPSpeechHandsfreeAudio` 1.0 to run `Carplay_Siri_1mic`. That
   routes the processed car mic into the A2I uplink `/output_mono_AirPlayUL`, which the daemon captures and sends on
   ch 14.
5. **Video (`video_manager`).**
   - Screen window `1280` (1280x480 @ 60, z-order 2000) plus an MM-Renderer context fed by the hardware decoder
     `/dev/name/local/mdev`.
   - The factory CarPlay context `cpvideoctx*` must be purged at start, or frames are accepted but nothing is drawn.
   - The phone renders a 1280x720 canvas. In crop mode (the default) the phone keeps its UI in the top 480 rows
     (bottom inset 240), and the panel shows those rows 1:1.
6. **Touch (`touch_probe`).** The car sends touch as UDP to port 30567 (from 160.48.199.115:30501), where the HMI also listens;
   the daemon binds the port only while Android Auto is on screen (one socket gets each datagram). One 112-byte
   datagram per touch on the Supra, 2-4 shorter datagrams (24..64 bytes) on 1440x540 BMW units.
   They are forwarded to the phone as input events on channel 7; the touchscreen is declared as 1280x720.
7. **Controller (`ccs_input`, `ccs_decoder.hpp`).** Observe-only MOST client on `DSYSMOSTServer` 4.5 that receives
   copies of the controller's FBlock 0xE5 status messages.

### Cluster and HUD arrows (`kombi_nav`)

- `aa_session` hands every channel 10 message (0x8000 and up) to a nav-status callback. `kombi_nav` copies the
  newest 0x8003 (status), 0x8006 (next step), 0x8007 (distances), and the older 0x8004 / 0x8005, and returns.
- A worker thread (4 ticks a second) decodes them (`kombi_nav_msg.hpp`: field numbers as in opencardev/aasdk,
  maneuver types as androidx `Maneuver.TYPE_*`) and runs the state machine (`kombi_nav.cpp`):
  - new maneuver: the full 97-frame route-start graph, maneuver 0 patched;
  - changed distance text: one in-place distance frame (at most twice a second);
  - no distance yet: wait up to 3 s, then `-` (never the template's own distance).
- The graph (`kombi_graph.hpp`) is a captured factory route start replayed byte for byte
  (`kombi_graph_template.hpp`, road names replaced by neutral text of the same length). Only fields proven on the
  car are changed: the direction enum in ManeuverDescription (0x0F), the distance text, its unit byte (0x01 mi,
  0x05 ft) and the red bar in ManeuverDistance (0x1D), and the road in TextResource (0x03).
- Direction enum: LEFT 6, RIGHT 7 and the destination flag 10 are car-verified. The other values follow the
  KOMBI's enum names (HALF_LEFT 2, TIGHT_LEFT 4, UTURN_LEFT 8, ROUNDABOUT_CCW 15, HIGHWAY_OUT_RIGHT 29, FORK_LEFT 37,
  ...) and may still need a parked check.
- `kombi_client` speaks SOME/IP over TCP: KOMBI magic cookies are skipped, a connection error gets one reconnect,
  and a non-E_OK reply stops the feature. The clear is the factory's own 6-frame cancel on a fresh connection, with
  the state-0 frame alone as a fallback.

### Touch frame layout

- Each datagram starts `B0 04 80 02` + BE32 length (datagram size - 8): a 16-byte header, then 8-byte records
  `[00 type flags seq][4 data bytes]`. Type `C5` starts a message, `C1` continues it, `C3` ends it; `seq` counts up by
  one per record, across datagrams.
- One touch message is 12 records. The Supra sends it in one 112-byte datagram; 1440x540 head units split it over
  several datagrams (for example 32 + 56 + 56 bytes). The daemon reassembles records by type and sequence, so both work;
  a lost datagram drops only that touch update.
- The data bytes of message records 4..11 form one 32-byte stream: `[flags][count]`, then `count` contacts of 6 bytes
  `x_hi x_lo y_hi y_lo size (state<<4 | id)`.
- State 1 = down, 2 = move, 3 = up. `id` is a stable finger id, used as the AA pointer id.
- Ranges: panel pixels, x 0..1279 / y 0..479 on the Supra, 0..1439 / 0..539 on a 1440x540 panel.
- Contacts are packed, so finger 1 straddles records 6 and 7.
- One physical touch reaches the phone as one tap. The forwarder uses the record `seq` to tell the next message
  from a gap: a duplicated message is skipped, a repeated "down" with no message lost in between is a move, and a
  finger it released itself (stuck for `NBT_TOUCH_IDLE_MS`, or missing from a message) is ignored while it is still
  reported, until the hardware sends its "up" or a new "down". Before, each of these reached the phone as an extra tap.

Parser: `src/touch_frame.hpp`; test `tests/test_touch_frame.cpp` runs against real captures from both layouts
(Supra pinch, F-series corner touches).

### Controller messages (CCS35up, `tests/fixtures/controls_2026-10-22_e5.txt`)

MOST FBlock 0xE5 instance 0x03, op Status, addressed to the HMI (dst 0x2023).

| fkt | Bytes | Meaning |
|---|---|---|
| 0xC46 | 8 | `[0]` counter, `[1..2]` BE16 detent position (+1 clockwise), `[3]` centre push (1 down / 0 up), `[4]` tilt as a 12-step clock (1 up, 4 right, 7 down, 10 left, 0 centred), `[7]` 0xff |
| 0xC47 / 0xC4F | 7 / 8 | One byte per hard key (0 released, 1 pressed, 2 held). Slots: 0 MENU, 1 BACK, 2 OPTIONS, 3 COM, 4 MEDIA, 5 NAV, 6 MAP |
| 0xC10 (old CCS) | 7 | `[2]` message counter (+1 per message), `[3..4]` BE16 knob position, `[6]` overflow |
| 0xC20 (old CCS) | 7 | One key event per message; `[2]` message counter; tilt 1/2/4/8 |

## The iDrive app (rhmiapp/)

NBT-AAD's menu entry is an iDrive **PluginApp**, the framework BMW uses for its own add-on apps (`SecurityMenu`,
`ActiveSeating`). At boot, HmiMain3D loads every `Plugins/<Name>/<Name>.manifest` under `/opt/hmi/ID5/data/ro` and
`/var/opt/hmi/data/ID5`. The entry button and its placement come from `NbtAad.xml`, so iDrive creates the row itself.
Nothing in HmiMain3D is patched.

| File | Purpose |
|---|---|
| `libNbtAad.so` | App code (`pluginAppInfo` entry point) |
| `NbtAad.manifest` | Tells CAppManager what to load |
| `NbtAad.xml` | Entry in Media/Radio (native state 20 = ST_Ent_Selection) after the Bluetooth audio row (focus 2270), opening the Start / Force Stop / Restart menu (raActions 21/22/23) |
| `NbtAad_Direct.xml` | Alternative: one-press entry that starts the daemon directly (raAction 2). Rename it to `NbtAad.xml` |
| `NbtAad_Multimedia.xml` | Alternative: iDrive's own Multimedia placement rule |
| `NbtAad_ALL_Texts.zip`, `NbtAad_ALL_Images.zip` | Text (6 languages) and icon DBs, regenerated by `python3 rhmiapp/make_db.py`. 55001 = 63x54 row icon, 55002 = 549x484 preview, 55010..55017 = connecting animation frames |

How it fits together:
- **Starting.** The app spawns `/opt/aa/nbt-aad` in a new process group, with the settings from `nbt-aad.env`.
  Daemon output goes to `/dev/null`; with Save logs to USB ticked the daemon opens `<stick>/nbt-aad.log` itself.
- **Single instance.** A running daemon owns UDP 127.0.0.1:52790. A second start sends `show` and exits. The
  app uses the same port for `quit` and `gear <n>`.
- **Status.** The daemon writes `"<idle|connecting|connected> <pid> <overlay>"` to `/dev/shmem/nbt-aad-state`. The
  file is written in place, because QNX `/dev/shmem` has no `rename()`; v1.8.3 fixed exactly that, which had made
  Force Stop and Restart see no daemon.
- **Polling.** Once a second the app polls the state file and switches the entry's text (`updateModel`). While
  connecting it animates the entry icon and shows iDrive's busy ring on the Start row.
- **Leaving the menu.** Once connected, the app triggers `focusEvent` 50 -> `NativeStateID` model 40, value 1
  (main menu), or 20 with `NBT_MENU_RETURN=media`. This is the mechanism ActiveSeating uses to leave its own screen.
  It is only accepted while our screen is on top.
- **Reverse.** The app subscribes to the car data property `driving.gear` and sends `gear <n>` to the daemon.
  Gear == `NBT_GEAR_REVERSE` hides the overlay for the camera.
- **Exit icon.** The phone's exit icon sends a VideoFocusRequest. The daemon answers it, hides the overlay and stops
  forwarding input. The menu entry brings the overlay back (FOCUSED is sent to the phone).

Not car-verified yet:
- the large preview (`previewModel="13"` -> preview-only hmiState 10, same pattern as BMW's SecurityMenu);
- that state 1 is the main menu on every car.

If the Media list rejects the XML, drop `previewModel`/`<hmiStates>`.

### Legacy HmiMain3D plugin (plugin/)

`plugin/` holds the earlier approach: `libhmiaad.so` + `NbtAadPlugin.xml` as a `hmiplugins/*.xml` bundle that adds a
`CHmiGenericMenuEntry` through iDrive's menu registry on the HMI thread, with code checks before calling two
non-exported functions. It is still built by `make all` (`plugin/plugin_check` loads it the way the framework does),
but the PluginApp above replaces it. To remove it, delete `NbtAadPlugin.xml` from `hmiplugins` and reboot. Optional
config was `/opt/aa/menu.cfg` (`menu=`, `text=`, `icon=`, `focus=`, `order=`) and `/opt/aa/menu.disable`.

## Building and tests

On the QNX SDP 6.5 VM (qcc, GCC 4.4.2):

```
make all        # nbt-aad, test_display, test_controls, plugin/, rhmiapp/
make rhmiapp    # only the iDrive app
```

- Don't add `-fPIC` to the daemon (it causes ELIBEXEC). The app and plugin are PIC shared objects built with the
  `_gpp` (GNU libstdc++) variant, to match HmiMain3D.
- QNX GCC 4.4.2 has no `__sync_*`/`__atomic_*`, so use pthread mutexes. Headers must include `<stddef.h>` for
  `size_t`.
- The daemon links `liba2i_client`, OpenSSL and `libscreen` from `lib/`, with RPATH `/opt/mm/lib`.
- The iDrive app needs `libAppFramework.A.so` and `libCoreFramework.A.so` from the firmware dump's
  `opt/hmi/ID5/lib`, copied into `rhmiapp/lib/`.

Host tests in `tests/` build with a Linux g++ (WSL is fine):

| Test | Covers |
|---|---|
| `test_sdr`, `test_sdr_drop` | Service discovery, channel drop list, audio mode, system-audio sink |
| `test_wpp` | Bluetooth wireless handshake framing |
| `test_bcl_detect` | Companion-app (BCL/Etch) link detection, incl. the BCL startup magic `12 34 56 78` |
| `test_spp_link_policy` | Silent links (MyBMW / AAIdrive) put on hold, Android Auto links never |
| `test_spp_direction` | Phone-opened (Android Auto) vs car-opened (BMW Apps) links, learning and switch-off |
| `test_session_lifecycle` | 400 session cycles (TSAN) |
| `test_touch`, `test_touch_frame` | Touch decode against real captures (Supra pinch, 1440x540 split frames) |
| `test_ccs_decoder`, `test_ccs_push`, `test_most_msg` | Controller decode (CCS35up + old CCS) against car captures |
| `test_ctl_wizard` | Wizard logic and env output |
| `test_voice_scenario` | Voice scenario default / off switch |
| `test_car_config` | `carplay.ini` parsing, NBT_SCREEN / NBT_VEHICLE |
| `test_pcm` | Ring buffer + resampler |
| `test_audio_mixer` | Browser mode mixer: ducking ramps, hold, saturation, gains |
| `rhmiapp_host_test` (+ `rhmiapp_fake_appframework`) | iDrive app logic against a fake framework |
| `test_kombi_nav_msg`, `test_kombi_graph`, `test_kombi_nav` (+ `src/kombi_nav.cpp src/kombi_client.cpp -lpthread`) | Cluster/HUD arrows: ch 10 decode, graph bytes, state machine + client against a fake KOMBI, iDrive-route watch, the daemon runtime end to end |
| `test_media_payload.py` | Media metadata payloads |

## Source layout

- `src/`:
  - core: `main` (daemon loop, overlay ownership, control port, state file), `aa_session`, `aa_cryptor`,
    `tcp_server`, `sdr_builder`, `car_config.hpp`, `nbt_env.hpp`;
  - Bluetooth and Wi-Fi: `wireless_setup`, `bluetooth_manager`, `spp_rereg.hpp`, `dsi_client`;
  - video and touch: `video_manager`, `touch_probe` (+ `touch_frame.hpp`, `touch_types.hpp`);
  - controller: `ccs_input` (+ `ccs_decoder.hpp`, `most_msg.hpp`, `ctl_wizard.hpp`);
  - audio: `audio_route` (`audioconn` wrapper), `voice_scenario`, `browser_audio` + `audio_mixer` (browser mode),
    `audio_manager`, `a2i_stream`, `pcm_utils`;
  - cluster/HUD arrows: `kombi_nav` (+ `kombi_nav_msg.hpp`, `kombi_graph.hpp`, `kombi_graph_template.hpp`),
    `kombi_client`;
  - other: `crash_handler`, `license_text.hpp`.
- `include/`: `dhu_sdp.h` (canned service discovery), `a2i_client.h`, `dsi_controller.h`, `screen_video.h`.
- `rhmiapp/`: the iDrive PluginApp. `plugin/`: the legacy HmiMain3D plugin.
- `test_controls.cpp`: the controller monitor / wizard. `test_display.cpp`: a display test.

## Known issues and next steps

- Force Stop / Restart: the v1.8.3 state-file fix needs a car test.
- iDrive still reacts to touches while Android Auto is shown (the UDP touch stream goes to both).
- CarPlay after Android Auto (v1.8.6): confirm the CarPlay picture is no longer zoomed on a `1180_480` car.
- Firewall: replace `pfctl -d` with one allow rule for the AA port (needs the car's live `pfctl` rules first).
- The older CCS controller: knob rotation (v1.8.4) needs a car confirmation, including the turn direction.
- `carplay.ini` screen codings such as `1180_480` or `900_540` are the CarPlay picture size, not always the panel: set
  `NBT_SCREEN` to the real panel size (e.g. `1280x480`, `1440x540`) if Android Auto doesn't fill the screen.
- Media through the head unit's own audio path (A2I) is experimental and not audible yet.
- Cluster/HUD arrows (`kombi_nav`): confirm on the car that the head unit reaches the KOMBI directly, run a drive,
  and check the direction values other than LEFT / RIGHT / destination (parked).

## License

NBT-AAD is licensed under the **PolyForm Noncommercial License 1.0.0** (`SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0`).
The full text is embedded in `src/license_text.hpp` and printed by `./nbt-aad --license`.

Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)

NBT-AAD is free of charge. Selling it, or bundling it with anything sold, is not permitted.
If you paid for it, you were scammed: official releases are only published at https://github.com/Sq00ky/Supra-iDrive6-Android-Auto.

Android Auto is a trademark of Google LLC. BMW, iDrive, Toyota and GR Supra are trademarks of their owners. This
project is not affiliated with or endorsed by any of them.
