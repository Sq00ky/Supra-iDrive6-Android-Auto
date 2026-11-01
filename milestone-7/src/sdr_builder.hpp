// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef SDR_BUILDER_HPP
#define SDR_BUILDER_HPP

#include <stdint.h>
#include <stddef.h>
#include <string>
#include <vector>

namespace nbt {

struct SdrIdentity {
    std::string carMake, carModel, carYear, vehicleId;
    std::string huMake, huModel, swBuild, swVersion;
    std::string btAdapterAddress;   // empty = no BluetoothChannel descriptor
    SdrIdentity();                  // Toyota / GR Supra defaults; BMW when carConfig().bmw
    void loadEnv();                 // NBT_CAR_MAKE, NBT_CAR_MODEL, NBT_CAR_YEAR, NBT_VEHICLE_ID,
                                    // NBT_HU_MAKE, NBT_HU_MODEL, NBT_SW_VERSION, NBT_BT_ADDR
};

// Takes the canned Google DHU ServiceDiscoveryResponse (dhu_sdp.h), keeps every
// channel descriptor untouched, replaces the identity strings (fields 2,3,4,5,7,8,9,10
// and the copies inside fields 16/17) and optionally appends a BluetoothChannel
// descriptor (ChannelDescriptor.bluetooth_channel = 6, adapter_address = 1).
std::vector<uint8_t> buildServiceDiscovery(const uint8_t* base, size_t baseLen, const SdrIdentity& id);

// Experiment hook for the main display's VideoConfig (channel 6). Environment (all optional):
//   NBT_VID_RES  = VideoResolution enum (1 = 800x480, 2 = 1280x720, 3 = 1920x1080)
//   NBT_VID_MW / NBT_VID_MH = margin width / height (VideoConfig fields 3 / 4)
//   NBT_VID_INSETS = "top,bottom,left,right"  -> AdditionalVideoConfig.insets (VideoConfig field 11)
// Returns true if anything was changed (prints what it did).
bool patchMainVideo(std::vector<uint8_t>& sdr);

// Declares the touchscreen (channel 7) as 1280 x NBT_TOUCH_CFG_H (default 720).
bool patchTouchConfig(std::vector<uint8_t>& sdr);

// Removes whole ChannelDescriptors (top-level field 1) by channel id. The list comes from
// NBT_SDR_DROP ("9", "8,9", "none" = keep everything); when unset and NBT_AUDIO_BT is on, the default
// is "9" (media sink only) so the phone plays media over Bluetooth A2DP. Car-verified 2026-10-23: the
// phone refuses the session without an audio sink ("No audio playback service") or without the mic
// ("No audio/mic"), so 8 and 14 must stay. DHU ids: 8 speech/guidance sink, 9 media sink, 14 mic.
// Returns the number of descriptors removed (prints what it did).
int dropSdrChannels(std::vector<uint8_t>& sdr);

// NBT_GUIDANCE_BT=1 (experiment, off by default): declare the channel 8 audio sink as SYSTEM_AUDIO instead of
// GUIDANCE. With no guidance sink (and no media sink in Bluetooth mode) the phone may mix nav prompts into its
// own audio, which already plays over Bluetooth A2DP. Returns the number of fields patched (0 or 1).
int retypeGuidanceSink(std::vector<uint8_t>& sdr);

// Android Auto audio sink channels. 8 and 9 come from the DHU template; 16 is added by addSystemAudioSink.
enum { AA_CH_GUIDANCE_AUDIO = 8, AA_CH_MEDIA_AUDIO = 9, AA_CH_SYSTEM_AUDIO = 16 };
inline bool isAaAudioSinkChannel(unsigned ch) {
    return ch == AA_CH_GUIDANCE_AUDIO || ch == AA_CH_MEDIA_AUDIO || ch == AA_CH_SYSTEM_AUDIO;
}

// Browser audio mode (or NBT_SYSTEM_SINK=1): appends a third audio sink, ChannelDescriptor{ id=16,
// media_sink_service{ codec PCM, audio_type 2 SYSTEM_AUDIO, 16 kHz / 16 bit / mono } }, so the phone sends
// notification chimes and UI sounds to the car instead of its own speaker. NBT_SYSTEM_SINK=0 leaves it out.
// Returns 1 if added. Does nothing if channel 16 already exists.
int addSystemAudioSink(std::vector<uint8_t>& sdr);

// Pure helper used by dropSdrChannels (host-tested): parses "8, 9,14" into ids (max 16).
size_t parseChannelList(const char* s, int* ids, size_t max_ids);

} // namespace nbt

#endif
