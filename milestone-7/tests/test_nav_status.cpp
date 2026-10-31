// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Host test: nav_status.hpp (generic decoder of the navigation status channel, ch 10).
// g++ -std=gnu++98 -Wall -Wextra -Iinclude -Isrc -o /tmp/test_nav_status tests/test_nav_status.cpp && /tmp/test_nav_status
// The state file goes to /dev/shmem/nbt-aad-nav.txt (on Linux only if /dev/shmem exists; otherwise it is skipped).
#include "nav_status.hpp"
#include <assert.h>

using namespace nbt;

static void put(std::vector<uint8_t>& o, uint64_t v) {
    while (v >= 0x80) { o.push_back((uint8_t)(v | 0x80)); v >>= 7; }
    o.push_back((uint8_t)v);
}
static void fieldV(std::vector<uint8_t>& o, int f, uint64_t v) { put(o, (uint64_t)(f << 3)); put(o, v); }
static void fieldB(std::vector<uint8_t>& o, int f, const std::vector<uint8_t>& b) {
    put(o, (uint64_t)(f << 3 | 2)); put(o, b.size()); o.insert(o.end(), b.begin(), b.end());
}
static void fieldS(std::vector<uint8_t>& o, int f, const char* s) {
    fieldB(o, f, std::vector<uint8_t>(s, s + strlen(s)));
}
static std::vector<uint8_t> frame(uint16_t id, const std::vector<uint8_t>& body) {
    std::vector<uint8_t> f;
    f.push_back((uint8_t)(id >> 8)); f.push_back((uint8_t)id);
    f.insert(f.end(), body.begin(), body.end());
    return f;
}

int main() {
    // made-up layout, only to exercise the decoder: step {maneuver {type, side}, road "Via Città"}, distance {m, "250", unit}
    std::vector<uint8_t> man, road, step, dist, body;
    fieldV(man, 1, 3); fieldV(man, 2, 2);
    fieldS(road, 1, "Via Citt\xC3\xA0");
    fieldB(step, 1, road); fieldB(step, 2, man);
    fieldV(dist, 1, 250); fieldS(dist, 2, "250"); fieldV(dist, 3, 1);
    fieldB(body, 1, step); fieldB(body, 3, dist);
    std::vector<uint8_t> icon(40, 0xFF); fieldB(body, 4, icon);    // opaque bytes (an image)

    std::string out; std::vector<std::string> strs;
    navpb::dump(&body[0], body.size(), out, strs, 0);
    printf("dump: %s\n", out.c_str());
    assert(out == "{1:{1:{1:\"Via Citt\xC3\xA0\"} 2:{1:3 2:2}} 3:{1:250 2:\"250\" 3:1} 4:<40 bytes>}");
    assert(strs.size() == 2 && strs[0] == "Via Citt\xC3\xA0" && strs[1] == "250");

    // truncated message must not crash or read past the end
    for (size_t cut = 0; cut < body.size(); ++cut) {
        std::string o; std::vector<std::string> s;
        navpb::dump(&body[0], cut, o, s, 0);
    }

    NavStatusLog log;
    std::vector<uint8_t> st; fieldV(st, 1, 1);
    log.onMessage(0x8003, frame(0x8003, st));         // guidance ACTIVE -> printed
    log.onMessage(0x8006, frame(0x8006, body));       // printed
    log.onMessage(0x8006, frame(0x8006, body));       // identical -> not printed
    std::vector<uint8_t> st2; fieldV(st2, 1, 2);
    log.onMessage(0x8003, frame(0x8003, st2));        // guidance inactive -> printed
    log.onMessage(0x8006, std::vector<uint8_t>(1, 0x80));   // too short: ignored
    printf("OK\n");
    return 0;
}
