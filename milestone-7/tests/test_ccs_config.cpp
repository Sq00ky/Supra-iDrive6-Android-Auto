// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Host test: the controller settings (car/ccs_config.hpp): defaults, parsing, and how they reach the decoder.
#include "car/ccs_config.hpp"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

using namespace nbt;
using namespace nbt::car;

static void clearAll() {
    unsetenv("NBT_KNOB_INST");
    unsetenv("NBT_KNOB_CCS_INST");
    unsetenv("NBT_KNOB_INVERT");
    unsetenv("NBT_PTT_ASSISTANT");
    unsetenv("NBT_PTT_SHORT");
    for (int s = 0; s < CCS_SLOTS; ++s) {
        char name[32];
        snprintf(name, sizeof(name), "NBT_KEY_%s", CcsDecoder::slotName(s));
        unsetenv(name);
    }
}

int main() {
    clearAll();
    {
        CcsConfig c = readCcsConfig();
        assert(c.inst == 0x03 && c.ccsInst == 0x01 && !c.invert && !c.pttAssistant && !c.pttShort);
        for (int s = 0; s < CCS_SLOTS; ++s) assert(!c.slotSet[s]);
        CcsDecoder d;
        applyCcsConfig(c, d);
        assert(d.slotKey(CcsDecoder::slotByName("BACK")) == AAK_BACK);     // the decoder's defaults stay
        assert(d.slotKey(CcsDecoder::slotByName("OPTIONS")) == 0);
        assert(d.slotKey(CcsDecoder::slotByName("MAP")) == AAK_NAVIGATION);
    }
    {
        setenv("NBT_KNOB_INST", "1f", 1);
        setenv("NBT_KNOB_CCS_INST", "02", 1);
        setenv("NBT_KNOB_INVERT", "1", 1);
        setenv("NBT_PTT_ASSISTANT", "1", 1);
        setenv("NBT_PTT_SHORT", "1", 1);
        setenv("NBT_KEY_MEDIA", "0x55", 1);    // base 0: hex and decimal both work
        setenv("NBT_KEY_COM", "0", 1);         // 0 is a value: this key stays with iDrive
        CcsConfig c = readCcsConfig();
        assert(c.inst == 0x1f && c.ccsInst == 0x02 && c.invert && c.pttAssistant && c.pttShort);
        CcsDecoder d;
        applyCcsConfig(c, d);
        assert(d.slotKey(CcsDecoder::slotByName("MEDIA")) == 0x55);
        assert(d.slotKey(CcsDecoder::slotByName("COM")) == 0);
        assert(d.slotKey(CcsDecoder::slotByName("BACK")) == AAK_BACK);
        // The instance reaches the decoder: instance 1F decodes, the default 03 no longer does.
        const uint8_t a[8] = { 1, 0x80, 0x01, 0, 0, 0, 0, 0xff }, b[8] = { 2, 0x80, 0x02, 0, 0, 0, 0, 0xff };
        CcsEvent e[4];
        assert(d.feed(0xE5, 0x1f, 0xC46, 0xC, a, 8, e, 4) == 0);
        assert(d.feed(0xE5, 0x1f, 0xC46, 0xC, b, 8, e, 4) == 1 && e[0].delta == -1);    // inverted
        assert(d.feed(0xE5, 0x03, 0xC46, 0xC, a, 8, e, 4) == 0);
    }
    clearAll();
    {
        setenv("NBT_PTT_ASSISTANT", "both", 1);
        CcsConfig c = readCcsConfig();
        assert(c.pttAssistant && c.pttShort);
    }
    clearAll();
    {
        setenv("NBT_PTT_ASSISTANT", "short", 1);
        CcsConfig c = readCcsConfig();
        assert(!c.pttAssistant && c.pttShort);
    }
    clearAll();
    {
        setenv("NBT_KNOB_INST", "", 1);        // empty keeps the default
        setenv("NBT_KEY_BACK", "", 1);
        setenv("NBT_KNOB_INVERT", "0", 1);     // present but off
        setenv("NBT_PTT_ASSISTANT", "0", 1);
        setenv("NBT_PTT_SHORT", "0", 1);
        CcsConfig c = readCcsConfig();
        assert(c.inst == 0x03 && !c.slotSet[CcsDecoder::slotByName("BACK")] && !c.invert && !c.pttAssistant && !c.pttShort);
    }
    printf("test_ccs_config OK\n");
    return 0;
}
