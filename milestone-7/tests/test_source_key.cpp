// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Host test: car::SourceKey on most::Client with the fake DsiClient. The steering-wheel MODE press counts (both of the
// car's copies), its release and other functions do not, and it shares the client with CcsInput without adding keys.
#include "car/ccs_input.hpp"
#include "car/source_key.hpp"
#include "fake_dsi_client.hpp"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

using namespace nbt;
using namespace nbt::car;
using namespace nbt::most;

static std::vector<std::string> g_seen;
static void onSource(const char* what, void* user) {
    assert(user == &g_seen);
    g_seen.push_back(what);
}
static void onKey(const CcsEvent&, void*) {}

static void sendFrame(uint16_t dst, uint8_t fblock, uint8_t inst, uint16_t fkt, uint8_t op, const std::vector<uint8_t>& payload) {
    Msg m;
    m.txType = 0; m.varTag = 2; m.varVal = 0; m.dst = dst; m.src = 0x5000;
    m.fblock = fblock; m.inst = inst; m.fkt = fkt; m.opType = op; m.hdrTag = 0;
    m.data = payload;
    std::vector<uint8_t> f = buildMessage(m);
    fake_dsi::data(kInfoMessage, &f[0], (uint32_t)f.size());
}

static void registered(uint32_t id) {
    std::vector<uint8_t> resp;
    putLe32(resp, id);
    putLe16(resp, 0x205D);
    fake_dsi::data(kInfoClientResponse, &resp[0], (uint32_t)resp.size());
}

int main() {
    // sourceKeyPress: the 2026-11-05 F36 capture's shapes (15/02 fkt 513, len 1).
    {
        const uint8_t on = 1, off = 0;
        assert(!strcmp(sourceKeyPress(0x513, &on, 1), "MODE"));
        assert(sourceKeyPress(0x513, &off, 1) == NULL);
        assert(sourceKeyPress(0x513, &on, 0) == NULL);
        assert(sourceKeyPress(0x513, NULL, 1) == NULL);
        assert(sourceKeyPress(0x512, &on, 1) == NULL);   // voice
        const uint8_t vol[4] = { 0, 0, 2, 1 };
        assert(sourceKeyPress(0x401, vol, 4) == NULL);   // volume buttons
    }

    fake_dsi::reset();
    g_seen.clear();
    Client c;
    CcsInput in;
    SourceKey key;
    assert(in.attach(c, CcsConfig(), &onKey, NULL));
    assert(key.attach(c, &onSource, &g_seen));
    assert(c.start("NBTAAD_MOST"));
    fake_dsi::attach();
    registered(93);
    const fake_dsi::Call& k = fake_dsi::calls.back();      // still only the controller's 8 keys
    static const uint16_t kKeys[] = { 0xE510, 0xE503, 0xE505, 0xE512, 0xE514, 0xE501, 0xE502, 0x1502 };
    assert(k.kind == fake_dsi::Call::kRequest && k.payload == buildRegisterDevCtrls(93, std::vector<uint16_t>(kKeys, kKeys + 8)));

    // One MODE press as the car sends it: to 0x2011 and 0x2022, then the release to both.
    sendFrame(0x2011, 0x15, 2, 0x513, kOpStatus, std::vector<uint8_t>(1, 1));
    sendFrame(0x2022, 0x15, 2, 0x513, kOpStatus, std::vector<uint8_t>(1, 1));
    sendFrame(0x2011, 0x15, 2, 0x513, kOpStatus, std::vector<uint8_t>(1, 0));
    sendFrame(0x2022, 0x15, 2, 0x513, kOpStatus, std::vector<uint8_t>(1, 0));
    assert(g_seen.size() == 2 && g_seen[0] == "MODE" && g_seen[1] == "MODE");
    // Other steering-wheel functions and other op types: nothing.
    sendFrame(0x2022, 0x15, 2, 0x512, kOpStatus, std::vector<uint8_t>(1, 1));
    sendFrame(0x2022, 0x15, 2, 0x513, kOpGet, std::vector<uint8_t>(1, 1));
    const uint8_t vol[4] = { 0, 0, 2, 1 };
    sendFrame(0x2022, 0x15, 2, 0x401, kOpStatus, std::vector<uint8_t>(vol, vol + 4));
    assert(g_seen.size() == 2);

    printf("test_source_key OK\n");
    return 0;
}
