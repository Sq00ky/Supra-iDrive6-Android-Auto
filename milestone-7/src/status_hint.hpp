// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#pragma once
// Why a waiting phone is stuck, for the iDrive status row (v1.9.113). The daemon decides the hint from what it saw,
// writes "net|hint" to /dev/shmem/nbt-aad-hint, and the iDrive app turns it into a text. A separate file from
// /dev/shmem/nbt-aad-status, so an older iDrive app keeps parsing that one unchanged.
#include <stdint.h>
#include <string.h>
namespace nbt {

const unsigned kHintRefusedRun = 5;     // ServiceRequestErrors in a row before the coding hint (1-2 are normal)
const uint32_t kHintNoBtS = 90;         // phone on Bluetooth this long without opening the Android Auto link
const uint32_t kHintNoJoinS = 600;      // how long "took the Wi-Fi details, never joined" stays shown
// Seconds after the Wi-Fi details went out with no TCP connect before "never joined" is called. Working phones in the
// car logs connected 0.6-13.8 s after the Bluetooth link opened; a phone that will not join holds Bluetooth ~36 s.
const int kHintJoinWaitS = 20;

// Wi-Fi state for the hint: what the car can offer the phone at all.
enum HintWifi { kHintWifiOk = 0, kHintWifiNone = 1, kHintWifiNoSsid = 2 };

// Priority (v1.9.115, Ronnie's order): 1. coding (nothing wireless works until it is fixed), 2. re-pair (nojoin, bt),
// 3. no usable Wi-Fi in the car (use USB), 4. Wi-Fi Direct up but no group name (set it by hand), then "connect phone".
// "coding", "nojoin", "bt", "nowifi", "nossid", "nophone" or "-". wifi: HintWifi. Times are monotonic seconds, 0 = not set. btConn: the car's Bluetooth
// connection state for the phone (2 = connected, 3 = service connected, 0/1 = no phone connected, -1 = not known yet:
// the car has not sent its device list, so no "connect phone" right after start).
inline const char* statusHint(unsigned refusedRun, uint32_t now, uint32_t noJoinAt, uint32_t btIdleSince, int btConn,
                              int wifi = kHintWifiOk) {
    if (refusedRun >= kHintRefusedRun) return "coding";
    if (noJoinAt && now - noJoinAt < kHintNoJoinS) return "nojoin";
    if (btIdleSince && btConn >= 2 && now - btIdleSince >= kHintNoBtS) return "bt";
    if (wifi == kHintWifiNone) return "nowifi";
    if (wifi == kHintWifiNoSsid) return "nossid";
    if (btConn == 0 || btConn == 1) return "nophone";
    return "-";
}

// While a link is "connecting": the re-pair hint replaces "Connecting to the phone..." once a try has gone
// kHintJoinWaitS without the phone joining (v1.9.115-status4), so a phone that keeps retrying does not hide it.
inline const char* connectingHint(uint32_t now, uint32_t noJoinAt) {
    return noJoinAt && now - noJoinAt < kHintNoJoinS ? "nojoin" : "-";
}

// kWaitNoPhone is last so the iDrive app's text table only grew at the end.
enum WaitingKind { kWaitUsb, kWaitCoding, kWaitNoJoin, kWaitNoBt, kWaitWfdNet, kWaitCarplay, kWaitEnableWfd, kWaitUsbOnly,
                   kWaitNoPhone, kWaitNoWifi, kWaitNoSsid };

// The text for a running daemon with no phone yet. usbOnly = "Android Auto over USB" only; net = "wfd" / "cp" / "-"
// (or empty from an older daemon, which keeps the old carplay.ini -> wfd0 -> USB chain).
inline WaitingKind waitingKind(bool usbOnly, const char* net, const char* hint, bool hasIni, bool hasWfd) {
    if (usbOnly) return kWaitUsb;
    if (!strcmp(hint, "coding")) return kWaitCoding;
    if (!strcmp(hint, "nojoin")) return kWaitNoJoin;
    if (!strcmp(hint, "bt")) return kWaitNoBt;
    if (!strcmp(hint, "nowifi")) return kWaitNoWifi;
    if (!strcmp(hint, "nossid")) return kWaitNoSsid;
    if (!strcmp(hint, "nophone")) return kWaitNoPhone;
    if (!strcmp(net, "wfd")) return kWaitWfdNet;
    if (!strcmp(net, "cp") || (!*net && hasIni)) return kWaitCarplay;
    if (hasWfd) return kWaitEnableWfd;
    return kWaitUsbOnly;
}

} // namespace nbt
