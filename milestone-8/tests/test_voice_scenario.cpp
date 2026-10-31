// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Host test for the AudioMatrix scenario request encoding (build with -DNBT_HOST_TEST).
#include "voice_scenario.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); ++g_fail; } } while (0)

int main() {
    std::vector<uint8_t> p;
    nbt::VoiceScenario::buildRequest(p, "Carplay_Siri_1mic");   // 17 chars + NUL = 18
    CHECK(p.size() == 4 + 20);
    CHECK(p[0] == 18 && p[1] == 0 && p[2] == 0 && p[3] == 0);
    CHECK(memcmp(&p[4], "Carplay_Siri_1mic", 18) == 0);
    CHECK(p[22] == 0 && p[23] == 0);

    nbt::VoiceScenario::buildRequest(p, "abc");                  // 3 + NUL = 4, no padding
    CHECK(p.size() == 8 && p[0] == 4);

    int r = 99;
    const uint8_t ok[4] = {0, 0, 0, 0}, bad[4] = {1, 0, 0, 0};
    CHECK(strcmp(nbt::VoiceScenario::resultText(0x80000003u, ok, 4, &r), "AddAmxAudioScenario") == 0 && r == 0);
    CHECK(strcmp(nbt::VoiceScenario::resultText(0x80000004u, bad, 4, &r), "RemoveAmxAudioScenario") == 0 && r == 1);
    CHECK(nbt::VoiceScenario::resultText(0x80000005u, ok, 4, &r) == NULL);
    CHECK(nbt::VoiceScenario::resultText(0x80000003u, ok, 2, &r) != NULL && r == -1);

    // On by default since v1.7.6; NBT_VOICE_SCENARIO=0 disables and activate is then a no-op.
    unsetenv("NBT_VOICE_SCENARIO");
    nbt::VoiceScenario d;
    d.init();
    CHECK(d.enabled());
    setenv("NBT_VOICE_SCENARIO", "0", 1);
    nbt::VoiceScenario v;
    v.init();
    CHECK(!v.enabled());
    v.activate("test");
    CHECK(!v.active());

    setenv("NBT_VOICE_SCENARIO", "1", 1);
    setenv("NBT_VOICE_SCENARIO_NAME", "Carplay_Siri_1mic_RefCh", 1);
    nbt::VoiceScenario w;
    w.init();
    CHECK(w.enabled() && strcmp(w.name(), "Carplay_Siri_1mic_RefCh") == 0);
    w.activate("open");  CHECK(w.active());
    w.activate("again"); CHECK(w.active());
    w.deactivate("close"); CHECK(!w.active());
    w.deactivate("close again"); CHECK(!w.active());

    printf(g_fail ? "test_voice_scenario: %d FAILED\n" : "test_voice_scenario OK\n", g_fail);
    return g_fail ? 1 : 0;
}
