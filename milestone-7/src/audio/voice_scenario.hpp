// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef VOICE_SCENARIO_HPP
#define VOICE_SCENARIO_HPP

#include <stdint.h>
#include <vector>

namespace nbt {

// Car microphone enable, the way Connectivity does it for CarPlay Siri (TelAudio mode 12 -> setAudioMatrix):
// DSI client of Nuance_ATP_Audio_Service.DSCPSpeechHandsfreeAudio 1.0, served by /opt/speech/bin/handsfreed,
// which forwards to natp's AudioMatrixService (nuance_audio_IAudioManager_activateScenario over /dev/scp_natp).
//   request 3 AddAmxAudioScenario(string name)    -> data 0x80000003 u32 result (0 = OK)
//   request 4 RemoveAmxAudioScenario(string name) -> data 0x80000004 u32 result (0 = OK)
// natp.cfg Carplay_Siri_1mic: mic /output_multich_1 ch1 -> SSE (Siri tuning) -> /input_stereo_4 ch2 "Carplay
// Uplink" -> dev-ioamp-router ASRC -> /output_mono_AirPlayUL (our A2I capture). Without an active scenario
// that capture delivers only zeros.
//
// Opt-in: NBT_VOICE_SCENARIO=1 (off by default). NBT_VOICE_SCENARIO_NAME overrides the scenario
// (default Carplay_Siri_1mic; natp.cfg also has Carplay_Siri_1mic_RefCh, Handsfree_Siri_1mic, ...).
class VoiceScenario {
public:
    VoiceScenario();

    // Reads the environment; opens the DSI client only when enabled. Safe to call once at start-up.
    void init();
    bool enabled() const { return enabled_; }
    const char* name() const { return name_; }

    // Called from the main loop (any thread). Idempotent: activate twice / deactivate when inactive are no-ops.
    void activate(const char* why);
    void deactivate(const char* why);
    bool active() const { return active_; }

    // Pure helpers (host-tested).
    static void buildRequest(std::vector<uint8_t>& out, const char* scenario);
    static const char* resultText(uint32_t id, const uint8_t* pl, uint32_t len, int* result);

private:
    bool send(uint32_t reqId, const char* what, const char* why);

    bool enabled_;
    bool active_;
    char name_[64];
};

} // namespace nbt

#endif
