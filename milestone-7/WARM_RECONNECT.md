# Experimental Start with Car reconnect recovery

This change has only been tested a few times on one BMW F30 with NBT Evo.
Startup and warm reconnection improved in those limited tests. It is not yet
validated for general use or other head units/phones.

## Known unresolved Bluetooth case

A phone-side Bluetooth disconnection/reconnection can leave Android Auto unable
to reconnect automatically. This was reproduced by toggling Bluetooth off/on. Force Stop followed by Start Android Auto restores
it in the reported test. This case remains unfixed; its failure mechanism has
not been confirmed. These changes should be reviewed and tested as a draft.

## Observed startup failure and recovery

The daemon can register the Android Auto UUID successfully yet remain idle
without a /dev/btspp* channel. No wireless handshake starts in that state, so a
counter of completed failed setup attempts alone cannot detect it. Separately,
completed setup attempts may exchange AP information without producing TCP.
A closed Bluetooth setup link or an ARP entry does not establish whether Wi-Fi
association succeeded or whether a passphrase is wrong.

The daemon publishes these optional fields after state, PID and overlay in
/dev/shmem/nbt-aad-state:

    setup_failures tcp_cycle missing_spp_seconds

The HMI app requests one guarded recovery after two completed setup failures
or a continuous absence of SPP nodes. The daemon rechecks its PID, TCP cycle,
active/pending session and current node presence. TCP acceptance and recovery
commit use the same mutex so recovery cannot stop an already accepted session.
The automatic path waits for the old process to exit and never escalates to
SIGTERM/SIGKILL. If exit is not observed within 12 s it abandons that attempt.
After exit, it relaunches quietly without requesting show or menu navigation.

Force Stop and Start with Car OFF suspend automatic recovery. The budget lives
in the HMI process and survives daemon replacement. It is rearmed after 30 s
of continuous connection or explicit Start/Restart. Changing timing profiles
does not itself replenish the budget.

The app also observes vehicle.powerStateHeadUnit and vehicle.statePWF through
its existing CDS subscription mechanism, using raw changes without assuming
ignition enum meanings. Actual values for these two properties were not seen
in the available car capture, so they must not be relied on as verified
ignition notifications. A distinct value would rearm recovery after 10 s of
settling; that interval is independent of the timing profile below.

A missing phone can consume the single recovery attempt while Bluetooth is
still off. No periodic unlimited restart loop is added. The unresolved
phone-Bluetooth-toggle case needs further diagnosis rather than an assumed fix.

## Timing profiles

| Wait | fast (default) | conservative |
| --- | ---: | ---: |
| Initial launch after HMI onInit | 3 s | 15 s |
| Continuous absence of SPP nodes | 3 s | 10 s |
| Quiet interval after actual daemon exit | 3 s | 10 s |

The optional /var/opt/aa/reconnect-profile file accepts exactly one token:
fast or conservative. An absent file selects fast; malformed content or other
read errors select conservative. The conservative profile restores the earlier
experimental timing values without rebuilding. Existing connections continue;
new decisions use the selected profile. An already scheduled relaunch keeps
its selected delay unless a power-state callback schedules a new one.

The fast profile removes configured waiting time, not the time required for
native services, Bluetooth/Wi-Fi/TLS or video initialization. Starting earlier
and shortening the quiet interval can affect reliability and need more car
testing. Conservative remains available for that comparison.

[Timing] markers in the daemon identify video initialization, SPP opening, TCP
acceptance, session preparation, UI state, overlay changes and shutdown. App
log times and these markers use CLOCK_MONOTONIC. Connected status and visible
projection are separate observations; neither marks the ignition-on instant.

## Building and deployment

On Windows, use a command prompt configured for QNX SDP 6.5, then run from this
directory:

    Build_QNX.cmd > build-qnx.log 2>&1
    echo %ERRORLEVEL%

The script rebuilds both nbt-aad and rhmiapp/libNbtAad.so and collects the output
in dist-warm-reconnect4. It does not install anything. A nonzero exit code means
the output must not be treated as a successful build.

Update the daemon and HMI library together using the normal installation
procedure and reload HMI so the new library is actually loaded. The v4 app's
3 s request threshold needs the matching daemon; an older daemon can reject it.
Keep existing deployment backups and installation-specific XML/resources.

## Validation performed

- QNX SDP 6.5 Windows builds and a few manual tests on one BMW F30 NBT Evo.
- C++98 host tests for recovery policy, both timing profiles, cancellation,
  PID/cycle changes, manual Stop and disabled autostart.
- Setup-progress tests, including 128 concurrent TCP-accept/recovery races.
- Existing AP-candidate and Bluetooth-link-detection tests.
- Host syntax checks for the daemon main translation unit and native HMI app.

Run the host tests from this directory with:

    sh tests/run_reconnect_tests.sh

Host tests do not validate QNX linking, proprietary HMI/CDS behavior or vehicle
reliability. No extensive repeated-cycle or broader vehicle/phone validation
has been performed. The change does not alter firmware, FSC, pairing, AP
credentials or the existing receive timeout.

Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
License: PolyForm Noncommercial 1.0.0; see ../LICENSE.md.
