// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef NBT_ENV_HPP
#define NBT_ENV_HPP

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "nbt_paths.hpp"

namespace nbt {

// Shared reading for the on/off settings.
// Unset, empty, or an unrecognised word returns -1 so the caller keeps its default.
// 0 / off / no / false return 0. 1 / on / yes / true return 1.
// A variable that is merely present does not turn a setting on.
inline int envFlag(const char* name) {
    const char* e = name ? getenv(name) : NULL;
    if (!e || !*e) return -1;
    if (strcmp(e, "0") == 0 || strcmp(e, "off") == 0 || strcmp(e, "no") == 0 ||
        strcmp(e, "false") == 0) return 0;
    if (strcmp(e, "1") == 0 || strcmp(e, "on") == 0 || strcmp(e, "yes") == 0 ||
        strcmp(e, "true") == 0) return 1;
    return -1;
}

// Child processes (audioconn, LayerManagerControl, pfctl, arp, ifconfig) inherit our environment, which comes from
// the HMI. Each of them was loading a graphics stack it never uses (`ldd:FATAL: Could not preload library
// libEGL_X.gpp.so` from audioconn, `unknown symbol: kdMain` on every LayerManagerControl run). Stripping LD_PRELOAD
// for children is the default since v1.9.69: unset means strip. NBT_CHILD_NO_PRELOAD=0 / off / no keeps it, which is
// only useful when a child is being debugged and needs the HMI's preloads.
inline bool childNoPreload() {
    return envFlag("NBT_CHILD_NO_PRELOAD") != 0;   // unset stays on
}

// HU-hosted wireless (Bluetooth RFCOMM -> Wi-Fi credentials -> TCP accept) is the default since
// v1.4.2 (car-verified). NBT_WIRELESS=0 / off / no restores the legacy outbound phone scanner.
inline bool wirelessEnabled() {
    return envFlag("NBT_WIRELESS") != 0;   // unset stays on
}

// iDrive knob / tilt / hard keys -> phone is the default since v1.4.3 (car-verified nbt-25).
// NBT_KNOB=0 / off / no disables it.
inline bool knobEnabled() {
    return envFlag("NBT_KNOB") != 0;
}

// The car's GPS fix (MOST 52/00 fkt 440, car/gps), default on. NBT_CAR_GPS=0 / off / no disables it.
inline bool carGpsEnabled() {
    return envFlag("NBT_CAR_GPS") != 0;
}

// The car's wheel speed (MOST 05/00 fkt 621, car/vehicle), default on. NBT_CAR_SPEED=0 / off / no disables it.
inline bool carSpeedEnabled() {
    return envFlag("NBT_CAR_SPEED") != 0;
}

// The car's gear (MOST 05/00 fkt 228, or the iDrive app until that reports, car/vehicle), default on. NBT_CAR_GEAR=0 / off / no
// disables the sensor. The reverse-gear handling has its own switch, NBT_GEAR_ACTION.
inline bool carGearEnabled() {
    return envFlag("NBT_CAR_GEAR") != 0;
}

// The car's parking brake (MOST 05/00 fkt CB5, car/vehicle), default on. NBT_CAR_PARKING_BRAKE=0 / off / no disables it.
inline bool carParkingBrakeEnabled() {
    return envFlag("NBT_CAR_PARKING_BRAKE") != 0;
}

// Video profile (PR #66). NBT_VID_RES: 1 = 800x480, 2 = 1280x720 (default), 3 = 1920x1080; anything else = 2. It is
// the canvas the phone renders, and decoder size, insets, DPI and touch all follow it (car_config.hpp).
inline int videoResolution() {
    const char* e = getenv("NBT_VID_RES");
    if (e && strcmp(e, "1") == 0) return 1;
    if (e && strcmp(e, "3") == 0) return 3;
    return 2;
}
inline int videoCanvasWidth() { return videoResolution() == 1 ? 800 : videoResolution() == 3 ? 1920 : 1280; }
inline int videoCanvasHeight() { return videoResolution() == 1 ? 480 : videoResolution() == 3 ? 1080 : 720; }

// NBT_VID_FPS (PR #66): "30" or "60"; anything else = 0 (not set). On the wire VideoConfig.fps is 1 = 60 fps and
// 2 = 30 fps (the phone app's VideoFrameRateType; obd2aa and open-android-auto agree, aasdk has the names swapped), and
// the DHU manifest we send carries 2: unset, the phone is asked for 30 fps (car logs: ~30 fps delivered) while the
// decoder is declared at 60, as before.
inline unsigned videoFpsSetting() {
    const char* e = getenv("NBT_VID_FPS");
    if (e && strcmp(e, "30") == 0) return 30u;
    if (e && strcmp(e, "60") == 0) return 60u;
    return 0u;
}
// What the phone is asked for: NBT_VID_FPS, else the manifest's 30.
inline unsigned videoRequestedFps() { return videoFpsSetting() ? videoFpsSetting() : 30u; }
// What the decoder (mdev format packet, MM-Renderer input) is declared with: NBT_VID_FPS, else 60 (unchanged).
inline unsigned videoDecoderFps() { return videoFpsSetting() ? videoFpsSetting() : 60u; }

// Maximum queued video frames. Default is a 15-frame cap
inline size_t videoQueueCapacity() {
    const char* e = getenv("NBT_VIDEO_QUEUE");
    if (!e || !*e) return 15;
    char* end = NULL;
    const unsigned long value = strtoul(e, &end, 10);
    return end != e && *end == 0 && value >= 1 && value <= 60 ? static_cast<size_t>(value) : 15;
}

// The audio mode (NBT_AUDIO_MODE, AudioMode, audioMode()) and the audio settings are in audio/config.hpp.

// Android Auto over USB through the head unit's AOA drivers (usb_accessory.cpp). Experimental, not car-verified, so
// off by default: it switches any Android phone plugged in while the daemon runs into accessory mode, which takes it
// away from iDrive's USB media (MTP) until it is replugged. On with the iDrive Settings checkbox "Android Auto over
// USB" (flag kUsbFlagName in the data dir; a hand-made one in the app dir also counts, nbt_paths.hpp) or NBT_USB=1 /
// on / yes / true; any other NBT_USB value (0, off) forces it off whatever the checkbox says. Read once at daemon
// start: a change applies at the next Start / Restart.
static const char* const kUsbFlagName = "usb-aa";
// Pure part (host-tested): NBT_USB's value (NULL = unset) and whether a flag file exists.
inline bool usbSettingOn(const char* env, bool flagFile) {
    if (env && *env)
        return strcmp(env, "1") == 0 || strcmp(env, "on") == 0 || strcmp(env, "yes") == 0 || strcmp(env, "true") == 0;
    return flagFile;
}
inline bool usbFlagFileSet() { return flagSet(kUsbFlagName); }
inline bool usbEnabled() { return usbSettingOn(getenv("NBT_USB"), usbFlagFileSet()); }

// With "Android Auto over USB" selected the wireless path is not offered at all: no Bluetooth SDP advert, no Wi-Fi
// credentials and the session starter refuses a TCP connection. Without this the phone is offered wireless Android Auto
// first and connects over Wi-Fi instead of the cable, which is the one thing USB mode is meant to avoid.
inline bool wirelessEnabledForTransport() { return wirelessEnabled() && !usbEnabled(); }

// "Prompts on car radio (BT, beta)" (iDrive Settings checkbox; the "speech-bt" flag, resolved by nbt_paths.hpp so a
// package install keeps it in its own data dir; NBT_SPEECH_BT=1 / 0 wins either way, same words as NBT_USB). Only
// meaningful in Bluetooth audio mode, where it turns the speech path on with the nav prompter defaults and keeps the
// ServiceDiscovery guidance sink on channel 8, so the car ducks the radio for a prompt instead of the phone mixing it
// into A2DP. Off by default: the BT path is not car-verified by its author. Read once at daemon start.
static const char* const kSpeechBtFlagName = "speech-bt";
inline bool speechBtEnabled() { return usbSettingOn(getenv("NBT_SPEECH_BT"), nbt::flagSet(kSpeechBtFlagName)); }

// "Use Phone GPS" (iDrive Settings checkbox, rhmiapp-44; flag kPhoneGpsFlagName, a hand-made one also counts;
// NBT_PHONE_GPS=1 / 0 wins either way, same words as NBT_USB). For cars without the navigation antenna, which have
// no MOST GPS fix or heading: the car's GPS is not read, and the ServiceDiscovery leaves out the location,
// compass, GPS satellite and accelerometer sensors, so the phone uses its own GPS, compass and motion sensors for
// everything built on them. Off = the car's GPS and heading go to the phone as before. Read at the start of the daemon
// (car GPS) and of each session (ServiceDiscovery): a change applies at the next Start / Restart.
static const char* const kPhoneGpsFlagName = "phone-gps";
inline bool phoneGpsFlagFileSet() { return flagSet(kPhoneGpsFlagName); }
inline bool phoneGpsEnabled() { return usbSettingOn(getenv("NBT_PHONE_GPS"), phoneGpsFlagFileSet()); }
// "Media Controls for LCI Steering Wheels" (iDrive Settings checkbox, rhmiapp-45; flag kLciMediaFlagName, a hand-made
// one also counts; NBT_LCI_MEDIA=1 / 0 wins either way, same words as NBT_USB). Off by default: the steering wheel's
// previous / next / play-pause (MOST 15/02, car/mfl.hpp, NBT_MFL_*) become Android media keys only with it on, and
// still only in a non-Bluetooth audio mode. Read at the start of the daemon.
static const char* const kLciMediaFlagName = "lci-media";
inline bool lciMediaFlagFileSet() { return flagSet(kLciMediaFlagName); }
inline bool lciMediaKeysEnabled() { return usbSettingOn(getenv("NBT_LCI_MEDIA"), lciMediaFlagFileSet()); }
// "CarPlay compatibility" (iDrive Settings checkbox, rhmiapp-54-cpcompat; flag kCpCompatFlagName, a hand-made one
// also counts; NBT_CP_COMPAT=1 / 0 wins either way, same words as NBT_USB). Off by default: for cars that use the
// factory CarPlay as well as Android Auto (issue #156). On = the three CarPlay fixes: Android Auto yields
// completely when CarPlay takes the screen (NBT_CP_YIELD), the factory CarPlay decoder context is detached instead of
// destroyed and re-attached afterwards (NBT_CP_PURGE), and CarPlay's window is top-level after a hand-over
// (NBT_CP_WINGRP). Each of those env settings, when present, still wins over the checkbox for its own fix. Read once at
// the start of the daemon: a change applies at the next Start / Restart.
static const char* const kCpCompatFlagName = "carplay-compat";
inline bool cpCompatFlagFileSet() { return flagSet(kCpCompatFlagName); }
inline bool cpCompatEnabled() {
    static int s = -1;   // read once: every caller in this run gets the same answer
    if (s < 0) s = usbSettingOn(getenv("NBT_CP_COMPAT"), cpCompatFlagFileSet()) ? 1 : 0;
    return s == 1;
}
// Pure (host-tested): an on/off env value (1 on yes true / 0 off no false) wins, anything else (unset, empty, junk)
// leaves the default.
inline bool settingOr(const char* env, bool dflt) {
    if (!env || !*env) return dflt;
    if (!strcmp(env, "1") || !strcmp(env, "on") || !strcmp(env, "yes") || !strcmp(env, "true")) return true;
    if (!strcmp(env, "0") || !strcmp(env, "off") || !strcmp(env, "no") || !strcmp(env, "false")) return false;
    return dflt;
}
inline bool cpYieldOn() { return settingOr(getenv("NBT_CP_YIELD"), cpCompatEnabled()); }
inline bool cpPurgeOn() { return settingOr(getenv("NBT_CP_PURGE"), !cpCompatEnabled()); }
inline bool cpWinGroupOn() { return settingOr(getenv("NBT_CP_WINGRP"), !cpCompatEnabled()); }

// With Android Auto over USB on (usbEnabled()), read the phone through libusbdi, not devc-serusb-aoap (which it
// stops for the phone): see usb_direct.hpp. On by default, since the /dev/oap* path loses TLS records whenever another
// client reads the node and the phone ends the session; NBT_USB_DIRECT=0 / off / no / false goes back to /dev/oap*.
// Pure part (host-tested): NBT_USB_DIRECT's value (NULL = unset).
inline bool usbDirectSettingOn(const char* env) {
    if (!env || !*env) return true;
    return !(strcmp(env, "0") == 0 || strcmp(env, "off") == 0 || strcmp(env, "no") == 0 || strcmp(env, "false") == 0);
}
inline bool usbDirectEnabled() { return usbDirectSettingOn(getenv("NBT_USB_DIRECT")); }

// NBT_USB_CLAIM_DRIVER=0: with NBT_USB_DIRECT, stop devc-serusb-aoap after it has started instead of keeping it from
// starting (usb_direct.hpp). On by default.
inline bool usbClaimDriverEnabled() {
    const char* e = getenv("NBT_USB_CLAIM_DRIVER");
    if (!e || !*e) return true;
    return !(strcmp(e, "0") == 0 || strcmp(e, "off") == 0 || strcmp(e, "no") == 0 || strcmp(e, "false") == 0);
}

inline unsigned long envHex(const char* name, unsigned long def) {
    const char* e = getenv(name);
    if (!e || !*e) return def;
    char* end = NULL;
    unsigned long v = strtoul(e, &end, 0);
    return (end && end != e) ? v : def;
}

// A whole number setting clamped to [lo, hi]; unset or unparsable returns def.
inline long envRange(const char* name, long def, long lo, long hi) {
    const char* e = getenv(name);
    long v = def;
    if (e && *e) {
        char* end = NULL;
        long x = strtol(e, &end, 0);
        if (end && end != e) v = x;
    }
    return v < lo ? lo : (v > hi ? hi : v);
}

// "Show directions at any distance" (Settings checkbox -> flag file): show the next maneuver at once instead of
// inside the last mile or the last few seconds. Either env var decides on its own and beats the checkbox. Pure so
// tests/test_kombi_nav.cpp covers the precedence.
static const char* const kNavAnyDistFlagName = "kombi-nav-anydist";
inline bool navAnyDistanceWanted(const char* envApproach, const char* envMi, bool flag) {
    if (envApproach && *envApproach) return false;    // NBT_KOMBI_NAV_APPROACH=... decides
    if (envMi && *envMi) return false;                // NBT_KOMBI_NAV_APPROACH_MI=... decides
    return flag;
}

// "Disable logging (performance test)": silence both sides. NBT_NO_LOG wins over the checkbox, as with every
// other setting. Pure so the host tests cover the precedence.
static const char* const kNoLogFlagName = "no-log";
inline bool noLogWanted(const char* envValue, bool flag) {
    if (envValue && *envValue) return envValue[0] != '0';
    return flag;
}

} // namespace nbt

#endif // NBT_ENV_HPP
