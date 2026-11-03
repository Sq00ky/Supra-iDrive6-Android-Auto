// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef SDR_BUILDER_HPP
#define SDR_BUILDER_HPP

#include "audio/channels.hpp"
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
// own audio, which already plays over Bluetooth A2DP. Never in browser audio mode (ch 8 is its guidance stream and
// ch 16 its SYSTEM_AUDIO sink): NBT_GUIDANCE_BT=1 is then ignored. Returns the number of fields patched (0 or 1).
int retypeGuidanceSink(std::vector<uint8_t>& sdr);

// The audio sink channels (AA_CH_*_AUDIO, isAaAudioSinkChannel) are in audio/channels.hpp.

// Browser audio mode (or NBT_SYSTEM_SINK=1): appends a third audio sink, ChannelDescriptor{ id=16,
// media_sink_service{ codec PCM, audio_type 2 SYSTEM_AUDIO, 16 kHz / 16 bit / mono } }, so the phone sends
// notification chimes and UI sounds to the car instead of its own speaker. NBT_SYSTEM_SINK=0 leaves it out.
// Returns 1 if added. Does nothing if channel 16 already exists.
int addSystemAudioSink(std::vector<uint8_t>& sdr);

// Keeps only the AudioConfigs with this sample rate in `channel`'s audio sink (ChannelDescriptor{ id } ->
// media_sink_service = 3 -> audio_configs = 3), rewriting the enclosing lengths. Does nothing when no offer has that
// rate, or when it is the only rate offered. Returns the number of offers removed.
int keepAudioSinkRate(std::vector<uint8_t>& sdr, unsigned channel, uint32_t rate);

// Browser audio mode: the guidance sink (ch 8) offers only NBT_GUIDANCE_RATE (default 48000; 0 = keep every offer).
// The DHU template offers 16 and 48 kHz and the phone picks without asking (car 2026-11-01: 48 kHz), so one offer
// leaves no doubt about the rate. Other modes unchanged. Returns the number of offers removed.
int limitGuidanceOffers(std::vector<uint8_t>& sdr);

// Pure helper used by dropSdrChannels (host-tested): parses "8, 9,14" into ids (max 16).
size_t parseChannelList(const char* s, int* ids, size_t max_ids);

// One AudioConfig an audio sink offers the phone (MediaSinkService.audio_configs, field 3:
// { sample_rate = 1, bit_depth = 2, channels = 3 }).
struct AaAudioConfig {
    uint32_t sample_rate, bits, channels;
    AaAudioConfig() : sample_rate(0), bits(0), channels(0) {}
};

// The AudioConfigs the SDR offers for `channel`, in order (ChannelDescriptor{ id = 1 } -> media_sink_service = 3 ->
// audio_configs = 3). The phone picks one by its position in this list. Empty if the channel has no audio sink.
std::vector<AaAudioConfig> sdrAudioConfigs(const uint8_t* sdr, size_t n, unsigned channel);

// AV START_INDICATION payload (after the 2-byte message id): { session = 1, configuration_index = 2 }. The index is
// the phone's choice among the channel's offered AudioConfigs (car 2026-11-01: guidance starts all said 1 = the
// second offer, 48 kHz, while our setup response had asked for config 0). Sets *session / *index (0xFFFFFFFF when
// absent) and returns true with *out filled if the index points into `offered`.
bool resolveAudioStart(const uint8_t* payload, size_t n, const std::vector<AaAudioConfig>& offered,
                       uint32_t* session, uint32_t* index, AaAudioConfig* out);

} // namespace nbt

#endif
