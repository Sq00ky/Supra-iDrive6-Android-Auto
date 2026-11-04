// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Host test: car::VolumeInput on most::Client with the fake DsiClient, from DSI frame to the volume callback. Also checks
// that it shares the client with car::CcsInput without adding keys or changing what the controller gets.
#include "car/ccs_input.hpp"
#include "car/volume_input.hpp"
#include "fake_dsi_client.hpp"
#include <assert.h>
#include <stdio.h>
#include <string>
#include <vector>

using namespace nbt;
using namespace nbt::car;
using namespace nbt::most;

static std::vector<int> g_levels;
static int g_max = -1;
static void onVolume(int level, int max, void* user) {
    assert(user == &g_levels);
    g_levels.push_back(level);
    g_max = max;
}

static std::string g_keys;
static void onKey(const CcsEvent& ev, void*) {
    if (ev.kind != 1) return;
    if (!g_keys.empty()) g_keys += ' ';
    g_keys += ev.what;
    g_keys += ev.down ? "+" : "-";
}

static void sendFrame(uint8_t fblock, uint8_t inst, uint16_t fkt, uint8_t op, const std::vector<uint8_t>& payload) {
    Msg m;
    m.txType = 0; m.varTag = 2; m.varVal = 0; m.dst = 0x2022; m.src = 0x1001;
    m.fblock = fblock; m.inst = inst; m.fkt = fkt; m.opType = op; m.hdrTag = 0;
    m.data = payload;
    std::vector<uint8_t> f = buildMessage(m);
    fake_dsi::data(kInfoMessage, &f[0], (uint32_t)f.size());
}

static void sendLevel(uint8_t level, uint8_t inst = 0, uint8_t op = kOpStatus, uint16_t fkt = 0xC08) {
    const uint8_t p[5] = { 0x01, 0x00, level, 0x00, 0x64 };
    sendFrame(0x20, inst, fkt, op, std::vector<uint8_t>(p, p + 5));
}

static void registered(uint32_t id) {
    std::vector<uint8_t> resp;
    putLe32(resp, id);
    putLe16(resp, 0x205D);
    fake_dsi::data(kInfoClientResponse, &resp[0], (uint32_t)resp.size());
}

static void noRoute(const View&, void*) {}

int main() {
    // Alone on the client: the car capture's levels reach the callback, and nothing is registered as a device controller.
    {
        fake_dsi::reset();
        g_levels.clear();
        Client c;
        VolumeInput v;
        assert(v.attach(c, &onVolume, &g_levels));
        assert(c.start("NBTAAD_MOST"));
        fake_dsi::attach();
        registered(93);
        for (size_t i = 0; i < fake_dsi::calls.size(); ++i)
            assert(!(fake_dsi::calls[i].kind == fake_dsi::Call::kRequest && fake_dsi::calls[i].id == kReqRegisterDevCtrls));

        const uint8_t up[] = { 8, 9, 11, 12, 13, 15 };
        for (int i = 0; i < 6; ++i) sendLevel(up[i]);
        assert(g_levels.size() == 6 && g_levels[0] == 8 && g_levels[5] == 15 && g_max == 100);
        sendLevel(15);                                   // the repeat ~ms later: dropped
        assert(g_levels.size() == 6);

        sendLevel(20, 0x01);                             // another AudioMaster instance: still the level
        assert(g_levels.size() == 7 && g_levels[6] == 20);
        sendLevel(21, 0, kOpGet);                        // not a Status
        sendLevel(22, 0, kOpStatus, 0x200);              // another function
        sendFrame(0x15, 2, 0xC08, kOpStatus, std::vector<uint8_t>(5, 1));   // another function block
        const uint8_t shortp[3] = { 1, 0, 30 };
        sendFrame(0x20, 0, 0xC08, kOpStatus, std::vector<uint8_t>(shortp, shortp + 3));
        sendFrame(0x20, 0, 0xC08, kOpStatus, std::vector<uint8_t>());       // empty: no crash
        assert(g_levels.size() == 7);

        // Link loss: the same level afterwards is shown again.
        fake_dsi::detach();
        fake_dsi::attach();
        registered(94);
        sendLevel(20, 0x01);
        assert(g_levels.size() == 8 && g_levels[7] == 20);
    }

    // With CcsInput on the same client: the controller's 8 keys are unchanged and both consumers get their messages.
    {
        fake_dsi::reset();
        g_levels.clear();
        g_keys.clear();
        Client c;
        CcsInput in;
        VolumeInput v;
        assert(in.attach(c, CcsConfig(), &onKey, NULL));
        assert(v.attach(c, &onVolume, &g_levels));
        assert(c.start("NBTAAD_MOST"));
        fake_dsi::attach();
        registered(93);
        const fake_dsi::Call& k = fake_dsi::calls.back();
        assert(k.kind == fake_dsi::Call::kRequest && k.id == kReqRegisterDevCtrls);
        static const uint16_t kKeys[] = { 0xE510, 0xE503, 0xE505, 0xE512, 0xE514, 0xE501, 0xE502, 0x1502 };
        assert(k.payload == buildRegisterDevCtrls(93, std::vector<uint16_t>(kKeys, kKeys + 8)));

        std::vector<uint8_t> keys(7, 0);
        keys[4] = 1;                                     // MEDIA down
        sendFrame(0xE5, 3, 0xC47, kOpStatus, keys);
        sendLevel(12);
        keys[4] = 0;
        sendFrame(0xE5, 3, 0xC47, kOpStatus, keys);
        assert(g_keys == "MEDIA+ MEDIA-");
        assert(g_levels.size() == 1 && g_levels[0] == 12);
    }

    // No room on the client: attach reports it.
    {
        Client c;
        VolumeInput v;
        for (int i = 0; i < Router::kMaxRoutes; ++i) assert(c.addRoute(0x05, 0, i, kAny, &noRoute, NULL));
        assert(!v.attach(c, &onVolume, &g_levels));
    }

    printf("test_volume_input OK\n");
    return 0;
}
