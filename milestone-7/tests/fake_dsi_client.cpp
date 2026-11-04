// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#include "fake_dsi_client.hpp"

#include <stdint.h>
#include <string.h>

namespace fake_dsi {

std::vector<Call> calls;
bool openResult = true;
std::string openedIface, openedRole;
int openedMajor = 0, openedMinor = 0;

static nbt::DsiClient* g_client = NULL;

// What the fake's onDataTramp takes in place of the library's CDSIIStream.
struct Frame {
    uint32_t id;
    const uint8_t* payload;
    uint32_t len;
};

void reset() {
    calls.clear();
    openResult = true;
    openedIface.clear();
    openedRole.clear();
    openedMajor = openedMinor = 0;
    g_client = NULL;
}

void attach() {
    if (g_client) nbt::DsiClient::onAttachedTramp(g_client);
}

void data(uint32_t id, const void* payload, uint32_t len) {
    Frame f;
    f.id = id;
    f.payload = static_cast<const uint8_t*>(payload);
    f.len = len;
    if (g_client) nbt::DsiClient::onDataTramp(g_client, &f);
}

bool opened() { return g_client != NULL; }

static void record(Call::Kind k, uint32_t id, const std::vector<uint8_t>* payload) {
    Call c;
    c.kind = k;
    c.id = id;
    if (payload) c.payload = *payload;
    calls.push_back(c);
}

} // namespace fake_dsi

namespace nbt {

DsiClient::DsiClient()
    : lib_(NULL), conn_(NULL), cli_(NULL), attached_(false), quiet_(false),
      attachedCb_(NULL), dataCb_(NULL), user_(NULL) {}

void DsiClient::setCallbacks(AttachedCb a, DataCb d, void* user) {
    attachedCb_ = a; dataCb_ = d; user_ = user;
}

void DsiClient::putU32(std::vector<uint8_t>& v, uint32_t x) {
    while (v.size() % 4) v.push_back(0);
    for (int i = 0; i < 4; ++i) v.push_back((uint8_t)(x >> (8 * i)));
}

void DsiClient::putString(std::vector<uint8_t>& v, const char* s) {
    uint32_t n = (uint32_t)strlen(s) + 1;
    putU32(v, n);
    v.insert(v.end(), s, s + n);
    while (v.size() % 4) v.push_back(0);
}

bool DsiClient::open(const char* iface, const char* role, int major, int minor) {
    fake_dsi::openedIface = iface;
    fake_dsi::openedRole = role;
    fake_dsi::openedMajor = major;
    fake_dsi::openedMinor = minor;
    fake_dsi::record(fake_dsi::Call::kOpen, 0, NULL);
    if (!fake_dsi::openResult) return false;
    fake_dsi::g_client = this;
    return true;
}

void DsiClient::onAttachedTramp(void* self) {
    DsiClient* c = static_cast<DsiClient*>(self);
    c->attached_ = true;
    if (c->attachedCb_) c->attachedCb_(c, c->user_);
}

void DsiClient::onDetachedTramp(void* self) {
    DsiClient* c = static_cast<DsiClient*>(self);
    c->attached_ = false;
}

void DsiClient::onDataTramp(void* self, void* istream) {
    DsiClient* c = static_cast<DsiClient*>(self);
    const fake_dsi::Frame* f = static_cast<const fake_dsi::Frame*>(istream);
    if (c->dataCb_) c->dataCb_(c, 0, f->id, 0, f->payload, f->len, c->user_);
}

bool DsiClient::setNotification(uint32_t attr) {
    if (!attached_) return false;
    fake_dsi::record(fake_dsi::Call::kNotify, attr, NULL);
    return true;
}

bool DsiClient::sendRequest(uint32_t requestId, const std::vector<uint8_t>& payload) {
    if (!attached_) return false;
    fake_dsi::record(fake_dsi::Call::kRequest, requestId, &payload);
    return true;
}

} // namespace nbt
