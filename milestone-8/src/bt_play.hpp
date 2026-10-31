// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef BT_PLAY_HPP
#define BT_PLAY_HPP

// NBT_BT_PLAY (EXPERIMENT, off by default): start the car's Bluetooth media player the way the iDrive
// "Bluetooth audio" selection does, once the car has GRANTED the Bluetooth source (audioconn exit status 0;
// a refused request sends nothing, because NBTMediaMainApp ignores activity for a source that is not active).
//
// Why: audioconn requests focus over AudioManagement "(no SourceActivity)" (its own help text). The BT source in
// NBTMediaMainApp (CPAuxiliaryInputDeviceBT, SourceActivity ON handler FUN_003e08fc, srcNr 1) only starts its
// player (mme / Cinemo reading the A2DP stream from /fs/avrcp00, AVRCP play to the phone) on a SourceActivity ON.
// Without it the source is routed but silent (MINI F60 report 2026-10-28).
//
//   NBT_BT_PLAY=1  DSI AuxInBTAudio.DMMAudioSource 9.0, request 2 requestActivate(activity = 0 PLAY)
//                  (response 0x80000003 echoes the activity; DSI 0 PLAY / 1 STOP / 2 PAUSE)
//   NBT_BT_PLAY=2  as 1, plus DSI BlueMedia.SGMedia 2.1, request 0 SendCommand(0 = MEDIA_COMMAND_START)
//                  (AVRCP PLAY to the phone; response 0x80000003 = result 0 OK, 1 TIMEOUT, 2 NOT_SUPPORTED,
//                  3 NOT_ALLOWED, 5 SYSTEM_ERROR)
//   NBT_BT_PLAY_DELAY  ms between the granted audioconn request and the play request (default 1500; the source
//                  must be allocated/active first, else NBTMediaMainApp drops it with "Source is not active!!!")
// Static analysis only (ghbta/FINDINGS_mediaapp.md, FINDINGS_mediaservice.md); interface versions inferred.

#include <stdint.h>
#include <vector>

namespace nbt {

class BtPlay {
public:
    BtPlay();
    void init();                       // reads NBT_BT_PLAY, opens the DSI clients (attach asynchronously)
    bool enabled() const { return mode_ > 0; }
    void play(const char* why);        // after NBT_BT_PLAY_DELAY ms, on a detached worker thread
    void stop(const char* why);        // requestActivate(STOP), immediately

    // Payload / response helpers (host-tested).
    static void buildU32(std::vector<uint8_t>& out, uint32_t v);
    // Returns a description for a known response id, NULL otherwise; *value = first u32 of the payload or -1.
    static const char* responseText(bool sgmedia, uint32_t id, const uint8_t* pl, uint32_t len, int* value);
    static const char* sgResultName(int r);

    // internal (worker thread)
    void sendNow(bool playing, const char* why);
    int delayMs() const { return delay_; }

private:
    int mode_;
    int delay_;
};

} // namespace nbt

#endif
