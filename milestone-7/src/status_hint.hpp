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

// "coding", "nojoin", "bt" or "-". Times are monotonic seconds, 0 = not set. btConn: the car's Bluetooth connection
// state for the phone (2 = connected, 3 = service connected, below = none).
inline const char* statusHint(unsigned refusedRun, uint32_t now, uint32_t noJoinAt, uint32_t btIdleSince, int btConn) {
    if (refusedRun >= kHintRefusedRun) return "coding";
    if (noJoinAt && now - noJoinAt < kHintNoJoinS) return "nojoin";
    if (btIdleSince && btConn >= 2 && now - btIdleSince >= kHintNoBtS) return "bt";
    return "-";
}

enum WaitingKind { kWaitUsb, kWaitCoding, kWaitNoJoin, kWaitNoBt, kWaitWfdNet, kWaitCarplay, kWaitEnableWfd, kWaitUsbOnly };

// The text for a running daemon with no phone yet. usbOnly = "Android Auto over USB" only; net = "wfd" / "cp" / "-"
// (or empty from an older daemon, which keeps the old carplay.ini -> wfd0 -> USB chain).
inline WaitingKind waitingKind(bool usbOnly, const char* net, const char* hint, bool hasIni, bool hasWfd) {
    if (usbOnly) return kWaitUsb;
    if (!strcmp(hint, "coding")) return kWaitCoding;
    if (!strcmp(hint, "nojoin")) return kWaitNoJoin;
    if (!strcmp(hint, "bt")) return kWaitNoBt;
    if (!strcmp(net, "wfd")) return kWaitWfdNet;
    if (!strcmp(net, "cp") || (!*net && hasIni)) return kWaitCarplay;
    if (hasWfd) return kWaitEnableWfd;
    return kWaitUsbOnly;
}

} // namespace nbt
