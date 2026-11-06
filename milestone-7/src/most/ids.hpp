// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef MOST_IDS_HPP
#define MOST_IDS_HPP

// The numbers of MOSTSubSystem.DSYSMOSTServer 4.5, one place for the daemon and the tools. Read from fromDSI at 0x12cca0
// (requests) and 0x12d984 (information, attributes) in libimp_sys_most_base_nbt.cpp-ne.so.17.
//
// Requests that have no constant on purpose, because this client observes and must never send them:
//   0x0 registerDevices, 0xD registerExtDevices, 0x6 changeInstID, and above all 0xC setCommunicationCtrl (its disable
//   mode turns MOST off).
// Not used so far: 0x4 unregisterDevices, 0x5 unregisterDevCtrls, 0x7 (u32), 0x9 (u16), 0xB setRegistryFilter,
//   0xE processMessageVector, 0x11 (u32), and information 0x80000008 (u32), 0x80000009 (registry vector).

#include <stdint.h>
#include <stddef.h>

namespace nbt {
namespace most {

static const char* const kServerInterface = "DSYSMOSTServer";
static const char* const kServerRole      = "MOSTSubSystem";
enum { kInterfaceMajor = 4, kInterfaceMinor = 5 };

// Requests.
const uint32_t kReqRegisterDevCtrls = 0x1;   // u32 client, u32 count, u16 keys[count]; skipped when the list is empty
const uint32_t kReqProcessMessage   = 0x2;   // u32 client, MessageType
const uint32_t kReqRegisterClient   = 0xF;   // registerMOSTClientExt(string): the name is a trace label

// Information.
const uint32_t kInfoClientResponse  = 0x80000000u;   // u32 clientId, u16 deviceId
const uint32_t kInfoMessage         = 0x80000001u;   // MessageType
const uint32_t kInfoChangeInstId    = 0x80000002u;   // u16 key, u8 inst
const uint32_t kInfoConnect         = 0x80000003u;   // DeviceInfo vector
const uint32_t kInfoKeyVector4      = 0x80000004u;   // 4, 5, 6: u16 key vectors for disconnect, activate, deactivate;
const uint32_t kInfoKeyVector5      = 0x80000005u;   //          which is which was not checked
const uint32_t kInfoKeyVector6      = 0x80000006u;
const uint32_t kInfoCleanUp         = 0x80000007u;   // CleanUpRequest, u16, u16

// Attributes.
const uint32_t kAttrCommState       = 0xC0000000u;   // u32
const uint32_t kAttrRegistry        = 0xC0000001u;   // u32 count, then u16 address, u16 FBlock << 8 | Inst
const uint32_t kAttrDeviceId        = 0xC0000002u;   // u16
const uint32_t kAttrCommCtrlMode    = 0xC0000003u;   // CommCtrlMode

// A clientId of all ones in kInfoClientResponse: the server refused the registration.
const uint32_t kClientRefused       = 0xffffffffu;

// The comm state value the vendor client's isCommunicationOk (0x109a54) accepts.
const uint32_t kCommStateOk         = 2;

// What the HMI's CHBMOSTClientImpl::componentConnected subscribes to, in its order. Without these the server sends no
// MOST messages at all. kAttrCommState follows once the registration is answered.
const uint32_t kClientSubscriptions[] = {
    kAttrDeviceId, kAttrCommCtrlMode, kAttrRegistry, kInfoMessage, kInfoChangeInstId,
    kInfoConnect, kInfoKeyVector4, kInfoKeyVector5, kInfoKeyVector6, kInfoCleanUp
};
enum { kClientSubscriptionCount = sizeof(kClientSubscriptions) / sizeof(kClientSubscriptions[0]) };

// MOST operation types. Properties and methods share the low numbers. Ops up to 8 are commands to a function block and
// the ones above are its replies, which is how the vendor client routes them (informationProcessMessage, 0x10c1b8).
enum OpType {
    kOpSet = 0x0, kOpGet = 0x1, kOpSetGet = 0x2, kOpIncrement = 0x3, kOpDecrement = 0x4, kOpGetInterface = 0x5,
    kOpStartResultAck = 0x6, kOpAbortAck = 0x7, kOpStartAck = 0x8,
    kOpErrorAck = 0x9, kOpProcessingAck = 0xA, kOpProcessing = 0xB, kOpStatus = 0xC, kOpResultAck = 0xD,
    kOpInterface = 0xE, kOpError = 0xF
};

// For logs. A slash marks the two names an op has: property / method.
inline const char* opName(unsigned op) {
    static const char* const names[16] = {
        "Set/Start", "Get/Abort", "SetGet/StartResult", "Increment", "Decrement", "GetInterface",
        "StartResultAck", "AbortAck", "StartAck", "ErrorAck", "ProcessingAck", "Processing",
        "Status/Result", "ResultAck", "Interface", "Error"
    };
    return op < 16 ? names[op] : "?";
}

} // namespace most
} // namespace nbt

#endif
