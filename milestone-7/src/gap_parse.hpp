// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
//
// Readers for BSS_GAP.SGBluetoothGap DSI payloads (libdsiservice FUN_001c6418 / FUN_001c58c4, Ghidra 2026-10-27):
//   u32       little-endian, 4-byte aligned
//   string    u32 length INCLUDING the NUL, the bytes, zero padding to 4
//   list      u32 count, then the elements (strings or u32s)
// DeviceListGet response 0x80000028 = u32 error + the five lists below; DeviceListChanged inform 0x80000001 = the
// same five lists + u32 reason. connection: 0 NOT_IN_RANGE, 1 IN_RANGE, 2 CONNECTED (ACL), 3 SERVICE_CONNECTED.
// Plain C++98, host-tested (tests/test_gap_parse.cpp).
#ifndef NBT_GAP_PARSE_HPP
#define NBT_GAP_PARSE_HPP

#include <stdint.h>
#include <stddef.h>
#include <string>
#include <vector>

namespace nbt {

class GapReader {
public:
    GapReader(const uint8_t* p, uint32_t n) : p_(p), n_(p ? n : 0), off_(0), ok_(true) {}
    bool ok() const { return ok_; }
    uint32_t left() const { return n_ - off_; }

    uint32_t u32() {
        if (!ok_ || left() < 4) { ok_ = false; return 0; }
        uint32_t v = (uint32_t)p_[off_] | ((uint32_t)p_[off_ + 1] << 8) | ((uint32_t)p_[off_ + 2] << 16) |
                     ((uint32_t)p_[off_ + 3] << 24);
        off_ += 4;
        return v;
    }
    std::string str() {
        uint32_t len = u32();
        if (!ok_) return std::string();
        uint32_t padded = (len + 3u) & ~3u;
        if (len > 4096 || padded > left()) { ok_ = false; return std::string(); }
        std::string s;
        for (uint32_t i = 0; i < len && p_[off_ + i]; ++i) s += (char)p_[off_ + i];
        off_ += padded;
        return s;
    }
    std::vector<std::string> strList() {
        std::vector<std::string> v;
        uint32_t n = u32();
        if (n > 64) { ok_ = false; return v; }
        for (uint32_t i = 0; ok_ && i < n; ++i) v.push_back(str());
        return v;
    }
    std::vector<uint32_t> u32List() {
        std::vector<uint32_t> v;
        uint32_t n = u32();
        if (n > 64) { ok_ = false; return v; }
        for (uint32_t i = 0; ok_ && i < n; ++i) v.push_back(u32());
        return v;
    }

private:
    const uint8_t* p_;
    uint32_t n_, off_;
    bool ok_;
};

struct GapDevice {
    std::string name, address;
    uint32_t cod, security, connection;
};

// withError: DeviceListGet response (leading u32 error); else the DeviceListChanged inform.
inline bool gapParseDeviceList(const uint8_t* p, uint32_t n, bool withError, std::vector<GapDevice>& out,
                               uint32_t* err) {
    GapReader r(p, n);
    uint32_t e = withError ? r.u32() : 0;
    if (err) *err = e;
    std::vector<std::string> names = r.strList(), addrs = r.strList();
    std::vector<uint32_t> cod = r.u32List(), sec = r.u32List(), con = r.u32List();
    if (!r.ok()) return false;
    out.clear();
    for (size_t i = 0; i < addrs.size(); ++i) {
        GapDevice d;
        d.name = i < names.size() ? names[i] : std::string();
        d.address = addrs[i];
        d.cod = i < cod.size() ? cod[i] : 0;
        d.security = i < sec.size() ? sec[i] : 0;
        d.connection = i < con.size() ? con[i] : 0;
        out.push_back(d);
    }
    return true;
}

// ServiceListGet response 0x8000002d = u32 error, str address, strlist service, u32list connection state,
// strlist profile, u32list version. ServiceListChanged inform 0x80000008 = the same without the error, + u32 reason.
struct GapServiceList {
    std::string address;
    std::vector<std::string> services, profiles;
    std::vector<uint32_t> states, versions;
};
inline bool gapParseServiceList(const uint8_t* p, uint32_t n, bool withError, GapServiceList& out, uint32_t* err) {
    GapReader r(p, n);
    uint32_t e = withError ? r.u32() : 0;
    if (err) *err = e;
    out.address = r.str();
    out.services = r.strList();
    out.states = r.u32List();
    out.profiles = r.strList();
    out.versions = r.u32List();
    return r.ok();
}

// GapErrorCode (libbssservice table @0x1f526c).
inline const char* gapErrorText(uint32_t e) {
    static const char* const k[] = { "OK", "TIMEOUT", "BUSY", "STATE", "LINKKEY", "DEFAULT", "REFUSED", "RFCOMM",
                                     "SECURITY", "MISSING_KEY", "INVALID_PARAM", "ALREADY_CONNECTED", "HW_NOT_READY",
                                     "NO_SLC", "PAGE_TIMEOUT", "DEVICE_NOT_IN_LIST" };
    return e < sizeof(k) / sizeof(k[0]) ? k[e] : "FAILED";
}

} // namespace nbt

#endif
