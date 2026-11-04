// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Host test: car::CcsInput on most::Client with the fake DsiClient. Replays a car capture from DSI frame to CcsEvent, then
// checks the routes, the registered keys and the voice button.
#include "car/ccs_input.hpp"
#include "fake_dsi_client.hpp"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <unistd.h>
#include <vector>

using namespace nbt;
using namespace nbt::car;
using namespace nbt::most;

static std::string g_seq;
static int g_rot = 0;
static void onEvent(const CcsEvent& ev, void* user) {
    assert(user == &g_seq);
    if (ev.kind == 2) { ++g_rot; return; }
    if (!g_seq.empty()) g_seq += ' ';
    g_seq += ev.what;
    g_seq += ev.cancel ? "!" : (ev.down ? "+" : "-");
}

static void sendFrame(uint8_t fblock, uint8_t inst, uint16_t fkt, uint8_t op, const std::vector<uint8_t>& payload) {
    Msg m;
    m.txType = 0; m.varTag = 2; m.varVal = 0; m.dst = 0x2022; m.src = 0x1001;
    m.fblock = fblock; m.inst = inst; m.fkt = fkt; m.opType = op; m.hdrTag = 0;
    m.data = payload;
    std::vector<uint8_t> f = buildMessage(m);
    fake_dsi::data(kInfoMessage, &f[0], (uint32_t)f.size());
}

static void noRoute(const View&, void*) {}

static void sendPtt(uint8_t state) { sendFrame(0x15, 2, 0x512, kOpStatus, std::vector<uint8_t>(1, state)); }

static void start(Client& c, CcsInput& in, const CcsConfig& cfg) {
    fake_dsi::reset();
    g_seq.clear();
    g_rot = 0;
    assert(in.attach(c, cfg, &onEvent, &g_seq));
    assert(c.start("NBTAAD_MOST"));
    fake_dsi::attach();
    std::vector<uint8_t> resp;
    putLe32(resp, 93);
    putLe16(resp, 0x205D);
    fake_dsi::data(kInfoClientResponse, &resp[0], (uint32_t)resp.size());
}

int main(int argc, char** argv) {
    const char* path = argc > 1 ? argv[1] : "tests/fixtures/controls_2026-10-23_push.txt";

    // The car capture, from DSI frame to event: the same sequence test_ccs_push gets from the decoder alone.
    {
        Client c;
        CcsInput in;
        start(c, in, CcsConfig());

        // The current 8 keys, on the response.
        const fake_dsi::Call& k = fake_dsi::calls.back();
        assert(k.kind == fake_dsi::Call::kRequest && k.id == kReqRegisterDevCtrls);
        static const uint16_t kKeys[] = { 0xE510, 0xE503, 0xE505, 0xE512, 0xE514, 0xE501, 0xE502, 0x1502 };
        assert(k.payload == buildRegisterDevCtrls(93, std::vector<uint16_t>(kKeys, kKeys + 8)));

        FILE* f = fopen(path, "r");
        if (!f) { printf("cannot open %s\n", path); return 1; }
        char line[512];
        while (fgets(line, sizeof(line), f)) {
            if (line[0] == '#') continue;
            unsigned inst, fkt, op;
            int off = 0;
            if (sscanf(line, "%x %x %x%n", &inst, &fkt, &op, &off) != 3) continue;
            std::vector<uint8_t> d;
            const char* p = line + off;
            unsigned b;
            int n;
            while (sscanf(p, " %2x%n", &b, &n) == 1) { d.push_back((uint8_t)b); p += n; }
            sendFrame(0xE5, (uint8_t)inst, (uint16_t)fkt, (uint8_t)op, d);
        }
        fclose(f);
        printf("sequence: %s\n", g_seq.c_str());
        assert(g_seq == "PUSH+ PUSH- PUSH+ PUSH- PUSH+ PUSH- PUSH+ PUSH- TILT_RIGHT+ TILT_RIGHT- "
                        "TILT_LEFT+ TILT_LEFT- MEDIA+ MEDIA-");
        assert(g_rot == 0);
    }

    // Any controller instance reaches the decoder, which picks its own. Other function blocks do not.
    {
        Client c;
        CcsInput in;
        CcsConfig cfg;
        cfg.inst = 0x10;
        start(c, in, cfg);
        const uint8_t a[8] = { 1, 0x80, 0x01, 0, 0, 0, 0, 0xff }, b[8] = { 2, 0x80, 0x03, 0, 0, 0, 0, 0xff };
        std::vector<uint8_t> first(a, a + 8), second(b, b + 8);
        sendFrame(0xE5, 0x03, 0xC46, kOpStatus, first);
        sendFrame(0xE5, 0x03, 0xC46, kOpStatus, second);
        assert(g_rot == 0);
        sendFrame(0xE5, 0x10, 0xC46, kOpStatus, first);
        sendFrame(0xE5, 0x10, 0xC46, kOpStatus, second);
        assert(g_rot == 1);
        sendFrame(0x05, 0x10, 0xC46, kOpStatus, second);
        assert(g_rot == 1);
    }

    // The voice button: on any instance, Status only, from the message's receive time.
    {
        Client c;
        CcsInput in;
        CcsConfig cfg;
        cfg.pttAssistant = true;
        start(c, in, cfg);

        sendPtt(1);
        sendPtt(0);                               // short: for iDrive
        assert(g_seq.empty());

        sendFrame(0x15, 7, 0x512, kOpStatus, std::vector<uint8_t>(1, 1));
        usleep(800000);
        sendFrame(0x15, 2, 0x512, kOpGet, std::vector<uint8_t>(1, 0));    // not a Status: ignored
        assert(g_seq.empty());
        sendFrame(0x15, 2, 0x401, kOpStatus, std::vector<uint8_t>(1, 0)); // another function: ignored
        assert(g_seq.empty());
        sendFrame(0x15, 7, 0x512, kOpStatus, std::vector<uint8_t>(1, 0)); // long: the Assistant key
        assert(g_seq == "PTT+ PTT-");

        sendFrame(0x15, 2, 0x512, kOpStatus, std::vector<uint8_t>());     // an empty payload does not crash
    }
    {
        Client c;
        CcsInput in;
        start(c, in, CcsConfig());                // NBT_PTT_ASSISTANT off
        sendPtt(1);
        usleep(800000);
        sendPtt(0);
        assert(g_seq.empty());
    }

    // Link loss with keys held: a cancel release for each, and the positions re-base. Copies of it change nothing.
    {
        Client c;
        CcsInput in;
        start(c, in, CcsConfig());
        std::vector<uint8_t> keys(7, 0);
        keys[1] = 1;                                // BACK down
        const uint8_t a[8] = { 1, 0x80, 0x01, 0, 4, 0, 0, 0xff };    // tilt right down
        sendFrame(0xE5, 3, 0xC47, kOpStatus, keys);
        sendFrame(0xE5, 3, 0xC46, kOpStatus, std::vector<uint8_t>(a, a + 8));
        assert(g_seq == "BACK+ TILT_RIGHT+");
        g_seq.clear();
        fake_dsi::detach();
        assert(g_seq == "TILT_RIGHT! BACK!");
        g_seq.clear();
        fake_dsi::attach();                         // re-registering: nothing more to release
        assert(g_seq.empty());
        std::vector<uint8_t> resp;
        putLe32(resp, 94);
        putLe16(resp, 0x205E);
        fake_dsi::data(kInfoClientResponse, &resp[0], (uint32_t)resp.size());
        assert(g_seq.empty());
        // The knob moved 256 detents while the link was down: no jump.
        const uint8_t b[8] = { 2, 0x81, 0x01, 0, 0, 0, 0, 0xff };
        g_rot = 0;
        sendFrame(0xE5, 3, 0xC46, kOpStatus, std::vector<uint8_t>(b, b + 8));
        assert(g_rot == 0);
    }

    // Link loss forgets a held voice button: the next release cannot start the Assistant.
    {
        Client c;
        CcsInput in;
        CcsConfig cfg;
        cfg.pttAssistant = true;
        start(c, in, cfg);
        sendPtt(1);
        fake_dsi::detach();
        usleep(800000);
        sendPtt(0);
        assert(g_seq.empty());
    }

    // No room on the client: attach reports it.
    {
        Client c;
        CcsInput in;
        for (int i = 0; i < Router::kMaxRoutes; ++i)
            assert(c.addRoute(0x05, 0, i, kAny, &noRoute, NULL));
        assert(!in.attach(c, CcsConfig(), &onEvent, &g_seq));
    }
    {
        Client c;
        CcsInput in;
        for (int i = 0; i < Client::kMaxKeys; ++i) assert(c.addDevCtrl((uint16_t)(0x4000 + i)));
        assert(!in.attach(c, CcsConfig(), &onEvent, &g_seq));
    }

    printf("test_ccs_input OK\n");
    return 0;
}
