// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef NBT_ENV_HPP
#define NBT_ENV_HPP

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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

// The audio mode (NBT_AUDIO_MODE, AudioMode, audioMode()) and the audio settings are in audio/config.hpp.

// Android Auto over USB through the head unit's AOA drivers (usb_accessory.cpp). Experimental, not car-verified, so
// off by default: it switches any Android phone plugged in while the daemon runs into accessory mode, which takes it
// away from iDrive's USB media (MTP) until it is replugged. On with the iDrive Settings checkbox "Android Auto over
// USB" (flag file kUsbFlagFile; a hand-made kUsbManualFile also counts) or NBT_USB=1 / on / yes / true; any other
// NBT_USB value (0, off) forces it off whatever the checkbox says. Read once at daemon start: a change applies at the
// next Start / Restart.
static const char* const kUsbFlagFile = "/var/opt/aa/usb-aa";
static const char* const kUsbManualFile = "/opt/aa/usb-aa";
// Pure part (host-tested): NBT_USB's value (NULL = unset) and whether a flag file exists.
inline bool usbSettingOn(const char* env, bool flagFile) {
    if (env && *env)
        return strcmp(env, "1") == 0 || strcmp(env, "on") == 0 || strcmp(env, "yes") == 0 || strcmp(env, "true") == 0;
    return flagFile;
}
inline bool usbFlagFileSet() { return access(kUsbFlagFile, F_OK) == 0 || access(kUsbManualFile, F_OK) == 0; }
inline bool usbEnabled() { return usbSettingOn(getenv("NBT_USB"), usbFlagFileSet()); }

// "Use Phone GPS" (iDrive Settings checkbox, rhmiapp-44; flag file kPhoneGpsFlagFile, a hand-made kPhoneGpsManualFile
// also counts; NBT_PHONE_GPS=1 / 0 wins either way, same words as NBT_USB). For cars without the navigation antenna,
// which have no MOST GPS fix or heading: the car's GPS is not read, and the ServiceDiscovery leaves out the location,
// compass, GPS satellite and accelerometer sensors, so the phone uses its own GPS, compass and motion sensors for
// everything built on them. Off = the car's GPS and heading go to the phone as before. Read at the start of the daemon
// (car GPS) and of each session (ServiceDiscovery): a change applies at the next Start / Restart.
static const char* const kPhoneGpsFlagFile = "/var/opt/aa/phone-gps";
static const char* const kPhoneGpsManualFile = "/opt/aa/phone-gps";
inline bool phoneGpsFlagFileSet() { return access(kPhoneGpsFlagFile, F_OK) == 0 || access(kPhoneGpsManualFile, F_OK) == 0; }
inline bool phoneGpsEnabled() { return usbSettingOn(getenv("NBT_PHONE_GPS"), phoneGpsFlagFileSet()); }

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

} // namespace nbt

#endif // NBT_ENV_HPP
