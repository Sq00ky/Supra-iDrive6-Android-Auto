// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Controller setup wizard: the Supra layout must need no env lines; a car with another instance, an inverted knob
// and BACK/OPTIONS swapped must get exactly the lines that fix it.
#include "ctl_wizard.hpp"
#include <stdio.h>
#include <string>
#include <vector>

using namespace nbt;
static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); ++g_fail; } } while (0)

struct Car {
    uint8_t inst; int pos; int dir;    // dir: +1 = position goes up when turned clockwise
    int slotOf[CCS_SLOTS];             // hard-key slot of MENU BACK OPTIONS COM MEDIA NAV MAP (-1 = not fitted)
    uint16_t keyFkt;
};

static void c46(CtlWizard& w, const Car& c, uint8_t push, uint8_t tilt, uint8_t inst) {
    uint8_t d[8] = { 0, (uint8_t)(c.pos >> 8), (uint8_t)c.pos, push, tilt, 0, 0, 0xff };
    w.feed(0xE5, inst, 0xC46, 0xC, d, 8);
}
static void keys(CtlWizard& w, const Car& c, int slot) {
    uint8_t d[8] = { 0, 0, 0, 0, 0, 0, 0, 7 };
    if (slot >= 0) d[slot] = 1;
    w.feed(0xE5, c.inst, c.keyFkt, 0xC, d, c.keyFkt == 0xC4F ? 8 : 7);
}

static void run(CtlWizard& w, Car c, bool noiseOn03) {
    w.begin(CtlWizard::ROTATE);
    c46(w, c, 0, 0, c.inst);                                  // first position: no delta yet
    for (int i = 0; i < 3; ++i) {
        if (noiseOn03) { uint8_t d[8] = { 0, 0, (uint8_t)(50 + (i ? 1 : 0)), 0, 0, 0, 0, 0xff }; w.feed(0xE5, 0x03, 0xC46, 0xC, d, 8); }
        c.pos += c.dir; c46(w, c, 0, 0, c.inst);
    }
    CHECK(w.stepDone());
    w.begin(CtlWizard::PUSH);  c46(w, c, 1, 0, c.inst); CHECK(w.stepDone()); CHECK(!w.released()); c46(w, c, 0, 0, c.inst); CHECK(w.released());
    w.begin(CtlWizard::TILT_UP); c46(w, c, 0, 1, c.inst); CHECK(w.stepDone()); c46(w, c, 0, 0, c.inst);
    w.begin(CtlWizard::TILT_RIGHT); c46(w, c, 0, 4, c.inst); CHECK(w.stepDone()); c46(w, c, 0, 0, c.inst);
    for (int k = 0; k < CCS_SLOTS; ++k) {
        w.begin(CtlWizard::KEY_FIRST + k);
        if (c.slotOf[k] < 0) { CHECK(!w.stepDone()); continue; }
        keys(w, c, c.slotOf[k]); CHECK(w.stepDone()); CHECK(!w.released());
        keys(w, c, -1); CHECK(w.released());
    }
}

static bool has(const std::vector<std::string>& v, const char* s) {
    for (size_t i = 0; i < v.size(); ++i) if (v[i] == s) return true;
    return false;
}

int main() {
    std::vector<std::string> env, notes;
    {   // Supra (reference): nothing to add.
        Car c = { 0x03, 1000, +1, { 0, 1, 2, 3, 4, 5, 6 }, 0xC47 };
        CtlWizard w; run(w, c, false); w.result(env, notes);
        CHECK(w.instance() == 0x03); CHECK(!w.inverted()); CHECK(w.pushSeen());
        CHECK(env.empty());
        CHECK(notes.empty());
    }
    {   // Other car: knob on instance 05 (with traffic on 03), position counts down, BACK/OPTIONS swapped, no MAP key.
        Car c = { 0x05, 1, -1, { 0, 2, 1, 3, 4, 5, -1 }, 0xC4F };   // pos 1 -> wraps below 0
        CtlWizard w; run(w, c, true); w.result(env, notes);
        CHECK(w.instance() == 0x05); CHECK(w.inverted());
        CHECK(has(env, "NBT_KNOB_INST=05"));
        CHECK(has(env, "NBT_KNOB_INVERT=1"));
        CHECK(has(env, "NBT_KEY_BACK=0"));       // slot 1 now holds OPTIONS (no AA key)
        CHECK(has(env, "NBT_KEY_OPTIONS=4"));    // slot 2 now holds BACK
        CHECK(env.size() == 4);
        CHECK(notes.size() == 1);                // MAP not pressed
        for (size_t i = 0; i < env.size(); ++i) printf("  env: %s\n", env[i].c_str());
        for (size_t i = 0; i < notes.size(); ++i) printf("  note: %s\n", notes[i].c_str());
    }
    {   // MENU moved to slot 6 (MAP's slot), MAP absent: slot 0 must be switched off, slot 6 must send HOME.
        Car c = { 0x03, 500, +1, { 6, 1, 2, 3, 4, 5, -1 }, 0xC47 };
        CtlWizard w; run(w, c, false); w.result(env, notes);
        CHECK(has(env, "NBT_KEY_MENU=0"));
        CHECK(has(env, "NBT_KEY_MAP=3"));
        CHECK(env.size() == 2);
    }
    {   // Wrong press: BACK pressed twice (slot 1 for both BACK and OPTIONS prompts) -> warning, no bogus mapping.
        Car c = { 0x03, 500, +1, { 0, 1, 1, 3, 4, 5, 6 }, 0xC47 };
        CtlWizard w; run(w, c, false); w.result(env, notes);
        bool warned = false;
        for (size_t i = 0; i < notes.size(); ++i) if (notes[i].find("both reported slot 1") != std::string::npos) warned = true;
        CHECK(warned);
    }
    {   // Older CCS protocol on E5/01 (fkt C10 knob, C20 key events): default instance -> only a note, no env lines.
        CtlWizard w;
        uint8_t r[7] = { 0x01, 0x1d, 0x05, 0x7f, 0xff, 0x1e, 0 };
        w.begin(CtlWizard::ROTATE);
        w.feed(0xE5, 1, 0xC10, 0xC, r, 7);
        for (int i = 1; i <= 3; ++i) { int p = 0x7fff + i; r[3] = (uint8_t)(p >> 8); r[4] = (uint8_t)p; w.feed(0xE5, 1, 0xC10, 0xC, r, 7); }
        CHECK(w.stepDone()); CHECK(w.ccsProtocol()); CHECK(w.instance() == 1); CHECK(!w.inverted());
        uint8_t k[7] = { 0x01, 0x1d, 0x05, 1, 0, 0x1e, 1 };
        w.begin(CtlWizard::PUSH); w.feed(0xE5, 1, 0xC20, 0xC, k, 7); CHECK(w.stepDone()); CHECK(!w.released());
        k[3] = 0; w.feed(0xE5, 1, 0xC20, 0xC, k, 7); CHECK(w.released());
        uint8_t t[7] = { 0x01, 0x1d, 0x05, 1, 1, 0x1d, 0 };
        w.begin(CtlWizard::TILT_UP); w.feed(0xE5, 1, 0xC20, 0xC, t, 7); CHECK(w.stepDone()); t[3] = 0; t[4] = 0; w.feed(0xE5, 1, 0xC20, 0xC, t, 7);
        t[3] = 1; t[4] = 2;
        w.begin(CtlWizard::TILT_RIGHT); w.feed(0xE5, 1, 0xC20, 0xC, t, 7); CHECK(w.stepDone()); t[3] = 0; t[4] = 0; w.feed(0xE5, 1, 0xC20, 0xC, t, 7);
        for (int key = 0; key < CCS_SLOTS; ++key) {
            uint8_t b[7] = { 0x01, 0x1d, 0x05, 1, 0, 0x00, (uint8_t)(1u << key) };
            w.begin(CtlWizard::KEY_FIRST + key); w.feed(0xE5, 1, 0xC20, 0xC, b, 7); CHECK(w.stepDone());
            b[3] = 0; w.feed(0xE5, 1, 0xC20, 0xC, b, 7); CHECK(w.released());
        }
        w.result(env, notes);
        CHECK(env.empty());
        CHECK(notes.size() == 1);   // "older CCS protocol ... send the -l log"
        for (size_t i = 0; i < notes.size(); ++i) printf("  ccs note: %s\n", notes[i].c_str());
    }
    printf(g_fail ? "test_ctl_wizard: %d FAILED\n" : "test_ctl_wizard: all passed\n", g_fail);
    return g_fail ? 1 : 0;
}
