// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Host test: car::InputActivity on most::Client with the fake DsiClient. Real presses count, the controller's routine
// status and key releases do not, and it shares the client with CcsInput without adding keys.
#include "car/ccs_input.hpp"
#include "car/input_activity.hpp"
#include "most_fixture.hpp"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

using namespace nbt;
using namespace nbt::car;
using namespace nbt::most;
using most_fixture::registered;

static std::vector<std::string> g_seen;
static void onInput(const char* what, void* user) {
    assert(user == &g_seen);
    g_seen.push_back(what);
}
static void onKey(const CcsEvent&, void*) {}

static void sendFrame(uint8_t fblock, uint8_t inst, uint16_t fkt, uint8_t op, const std::vector<uint8_t>& payload) {
    Msg m;
    m.txType = 0; m.varTag = 2; m.varVal = 0; m.dst = 0x2022; m.src = 0x1001;
    m.fblock = fblock; m.inst = inst; m.fkt = fkt; m.opType = op; m.hdrTag = 0;
    m.data = payload;
    std::vector<uint8_t> f = buildMessage(m);
    fake_dsi::data(kInfoMessage, &f[0], (uint32_t)f.size());
}
static std::vector<uint8_t> bytes(const uint8_t* p, size_t n) { return std::vector<uint8_t>(p, p + n); }

int main() {
    // mflPress: the volcap capture's shapes.
    {
        const uint8_t up[4] = { 0, 0, 2, 1 }, down[4] = { 0, 0, 1, 1 }, rel[4] = { 0, 0, 0, 1 };
        assert(!strcmp(mflPress(0x401, up, 4), "VOLUME_UP"));
        assert(!strcmp(mflPress(0x401, down, 4), "VOLUME_DOWN"));
        assert(!strcmp(mflPress(0xC01, up, 4), "VOLUME_UP"));
        assert(mflPress(0x401, rel, 4) == NULL);
        assert(mflPress(0x401, up, 2) == NULL);
        const uint8_t on = 1, off = 0;
        assert(!strcmp(mflPress(0x512, &on, 1), "VOICE"));
        assert(mflPress(0x512, &off, 1) == NULL);
        assert(mflPress(0x777, up, 4) == NULL);
        assert(mflPress(0x401, NULL, 4) == NULL);
    }

    fake_dsi::reset();
    g_seen.clear();
    Client c;
    CcsInput in;
    InputActivity act;
    assert(in.attach(c, CcsConfig(), &onKey, NULL));
    assert(act.attach(c, CcsConfig(), &onInput, &g_seen));
    assert(c.start("NBTAAD_MOST"));
    fake_dsi::attach();
    registered(93);
    const fake_dsi::Call& k = fake_dsi::calls.back();      // still only the controller's 8 keys
    static const uint16_t kKeys[] = { 0xE510, 0xE503, 0xE505, 0xE512, 0xE514, 0xE501, 0xE502, 0x1502 };
    assert(k.kind == fake_dsi::Call::kRequest && k.payload == buildRegisterDevCtrls(93, std::vector<uint16_t>(kKeys, kKeys + 8)));

    // Routine controller status (first position = baseline, the same position again): nothing.
    const uint8_t pos1[8] = { 1, 0x80, 0x10, 0, 0, 0, 0, 0xff }, pos1b[8] = { 2, 0x80, 0x10, 0, 0, 0, 0, 0xff };
    sendFrame(0xE5, 3, 0xC46, kOpStatus, bytes(pos1, 8));
    sendFrame(0xE5, 3, 0xC46, kOpStatus, bytes(pos1b, 8));
    assert(g_seen.empty());
    // A turn counts.
    const uint8_t pos2[8] = { 3, 0x80, 0x12, 0, 0, 0, 0, 0xff };
    sendFrame(0xE5, 3, 0xC46, kOpStatus, bytes(pos2, 8));
    assert(g_seen.size() == 1);
    // OPTIONS (unmapped, never sent to the phone) counts on the press, not on the release.
    std::vector<uint8_t> keys(7, 0);
    keys[2] = 1;
    sendFrame(0xE5, 3, 0xC47, kOpStatus, keys);
    assert(g_seen.size() == 2 && g_seen[1] == "OPTIONS");
    keys[2] = 0;
    sendFrame(0xE5, 3, 0xC47, kOpStatus, keys);
    assert(g_seen.size() == 2);
    // Steering wheel: volume press and voice press count; releases and other functions do not.
    const uint8_t vup[4] = { 0, 0, 2, 1 }, vrel[4] = { 0, 0, 0, 1 };
    sendFrame(0x15, 2, 0x401, kOpStatus, bytes(vup, 4));
    sendFrame(0x15, 2, 0x401, kOpStatus, bytes(vrel, 4));
    sendFrame(0x15, 2, 0x512, kOpStatus, std::vector<uint8_t>(1, 1));
    sendFrame(0x15, 2, 0x512, kOpStatus, std::vector<uint8_t>(1, 0));
    sendFrame(0x15, 2, 0x999, kOpStatus, bytes(vup, 4));
    sendFrame(0x15, 2, 0x401, kOpGet, bytes(vup, 4));
    assert(g_seen.size() == 4 && g_seen[2] == "VOLUME_UP" && g_seen[3] == "VOICE");
    // Link loss re-bases the controller: the first position afterwards is no turn.
    fake_dsi::detach();
    fake_dsi::attach();
    registered(94);
    const uint8_t pos3[8] = { 1, 0x81, 0x40, 0, 0, 0, 0, 0xff };
    sendFrame(0xE5, 3, 0xC46, kOpStatus, bytes(pos3, 8));
    assert(g_seen.size() == 4);

    printf("test_input_activity OK\n");
    return 0;
}
