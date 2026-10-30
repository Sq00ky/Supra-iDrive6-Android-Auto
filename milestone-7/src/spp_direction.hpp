// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef SPP_DIRECTION_HPP
#define SPP_DIRECTION_HPP

// Who opened a /dev/btsppN link: the phone, or the car.
//
// Both links land on the same node (Connectivity's classic SPP slot BtCtrl/ExtBox1 = /dev/btspp0), so the node name
// cannot tell them apart; the direction can:
//  - Android Auto: the PHONE opens the link. The AA app finds our UUID 4DE17A00-... in the car's SDP records and
//    connects to it (createRfcommSocketToServiceRecord; open-android-auto wifi-projection.md, phase 1). The Bluetooth
//    stack announces an incoming request with the GAP inform ServiceRequest (0x80000009: name, address, service;
//    libbssservice EvntServiceRequest, gap event 0x27) before Connectivity accepts it and the node appears.
//  - BMW Apps (MyBMW / BMW Connected; AAIdrive rides on it): the CAR opens the link. Connectivity sends
//    ServiceConnect(BtCtrl, ExtBox1, phone, "SPP") itself (A2DP up, boot reconnect, DisconnectTimerSPP), to the
//    phone's plain Serial Port service where the BMW app listens; NBTMediaMainApp's A4AHandler then talks BCL on
//    /dev/btspp0. No ServiceRequest precedes these.
//
// So: a node that appears without a fresh ServiceRequest belongs to the car's BMW Apps and is not ours to read or
// write. The rule is only enforced once this car has shown it (a phone-opened link answered like Android Auto),
// and it switches itself off for good if a car-opened link ever answers like Android Auto. Plain C++98, no locking
// (callers lock); host-tested in tests/test_spp_direction.cpp.

#include <stdint.h>
#include <string>

namespace nbt {

class SppDirection {
public:
    // kUnknown: not learned yet (v1.8.48 behaviour). kPhoneOpensAa: Android Auto came on a phone-opened link, so
    // car-opened links are left alone. kMixed: Android Auto also came on a car-opened link: never gate.
    enum Model { kUnknown = 0, kPhoneOpensAa = 1, kMixed = 2 };

    SppDirection() : windowS_(15), model_(kUnknown), haveReq_(false), reqAt_(0), requests_(0) {}

    void setWindowSeconds(uint32_t s) { windowS_ = s == 0 ? 1 : s; }
    uint32_t windowSeconds() const { return windowS_; }
    Model model() const { return model_; }
    void setModel(Model m) { model_ = m; }
    unsigned requestsSeen() const { return requests_; }

    // Bluetooth thread: ServiceRequest (or ServiceRequestComplete) inform for an incoming link.
    void onPhoneRequest(const std::string& service, uint32_t now) {
        haveReq_ = true;
        reqAt_ = now;
        reqService_ = service;
        ++requests_;
    }

    // Wireless thread: a new link node appeared. True = the phone opened it (a request no older than the window);
    // the request is used up either way, so one request accounts for one link.
    bool takePhoneOpened(uint32_t now, std::string* service) {
        bool fresh = haveReq_ && (int32_t)(now - reqAt_) >= 0 && (now - reqAt_) <= windowS_;
        if (fresh && service) *service = reqService_;
        haveReq_ = false;
        return fresh;
    }

    // A link we handled has ended. spoke = at least one Android Auto wireless frame came back. Returns true when the
    // model changed (the caller logs and saves it).
    bool onLinkResult(bool phoneOpened, bool spoke) {
        if (!spoke || model_ == kMixed) return false;
        if (!phoneOpened) { model_ = kMixed; return true; }
        if (model_ == kUnknown) { model_ = kPhoneOpensAa; return true; }
        return false;
    }

    // Leave car-opened links alone? forced: NBT_SPP_DIRECTION=1 (enforce without having learned it).
    bool gate(bool forced) const { return model_ != kMixed && (forced || model_ == kPhoneOpensAa); }

    static const char* modelName(Model m) {
        return m == kPhoneOpensAa ? "phone-opens-aa" : (m == kMixed ? "mixed" : "unknown");
    }
    static Model parseModel(const std::string& s) {
        if (s.compare(0, 14, "phone-opens-aa") == 0) return kPhoneOpensAa;
        if (s.compare(0, 5, "mixed") == 0) return kMixed;
        return kUnknown;
    }

private:
    uint32_t windowS_;
    Model model_;
    bool haveReq_;
    uint32_t reqAt_;
    std::string reqService_;
    unsigned requests_;
};

}  // namespace nbt
#endif
