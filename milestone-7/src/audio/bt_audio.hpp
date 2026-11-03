// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef AUDIO_BT_AUDIO_HPP
#define AUDIO_BT_AUDIO_HPP

// Bluetooth audio mode (NBT_AUDIO_MODE=bt, the default again since v1.8.66): AA audio stays on the phone's Bluetooth
// A2DP link; we only select the car's BT source, and optionally play guidance on A2I (NBT_SPEECH, speech_out.hpp).
//
// NBT_BT_CONN=<connId> (default 0xc7; 0 = old AudioManagement focus form `-b <src> <sink>`): request the car's
// Bluetooth source through the ConnectionMaster (`audioconn -b <connId>`, BuildHighSyncConnection: the one call the
// iDrive HMI makes when Bluetooth audio is picked, HmiMain3D RE 2026-10-29). CAR-VERIFIED (MINI F60, v1.8.35, 3 of 3
// sessions): granted at once and music starts by itself, even from FM radio, where the focus form was refused for all
// retries. `audioconn -l` lists eSRC_A2DP as ConnId 0xc7 (AuxInBTAudio srcNr 1) on the Supra and the MINI F60.
//   NBT_BT_SRC / NBT_BT_SINK  focus form source / sink (default 0x3a eSRC_A2DP / 0x1 eSNK_SPEAKERS)
//   NBT_BT_ROUTE_DELAY_MS     wait this long after the session starts before running audioconn (default 1500)
//   NBT_BT_REFOCUS            re-request the source when the phone closes the mic (default on)
//   NBT_BT_RELEASE=1          release the source when the session ends
//   NBT_BT_PLAY               experiment: SourceActivity PLAY once the source is granted (bt_play.hpp)

#include "audio/audio_route.hpp"
#include "audio/backend.hpp"
#include "audio/bt_play.hpp"
#include "audio/speech_out.hpp"
#include <stddef.h>
#include <stdint.h>

namespace nbt {

class BtAudio : public AudioBackend {
public:
    BtAudio();

    // Start-up: reads the environment, finds audioconn, starts NBT_BT_PLAY and NBT_SPEECH.
    void init();

    virtual void onSessionStarted();
    virtual void onFrame(uint8_t channel, const uint8_t* pcm, size_t len, uint64_t pts);
    virtual void onStreamState(uint8_t channel, bool started, uint32_t rate, uint32_t channels);
    virtual void onSessionEnded();
    virtual void onMicOpened();
    virtual void onMicClosed();
    virtual void cancelPending();
    virtual void stop();

private:
    BtAudio(const BtAudio&);
    BtAudio& operator=(const BtAudio&);

    static void grantedThunk(void* self);   // audioconn worker thread
    static int routeDelayMs();
    static bool refocusEnabled();
    bool requestSource();
    void releaseSource();
    void describe(char* out, size_t n);
    void snapshot(bool listening);

    AudioRoute route_;
    uint32_t src_;     // eSRC_A2DP (audioconn -l: ConnId 0xc7 AuxInBTAudio)
    uint32_t sink_;    // eSNK_SPEAKERS
    uint32_t conn_;
    BtPlay play_;
    SpeechOut speech_;
    int snaps_;        // Assistant snapshots taken (first 3 turns)
};

} // namespace nbt

#endif // AUDIO_BT_AUDIO_HPP
