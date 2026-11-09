// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef SDR_BUILDER_HPP
#define SDR_BUILDER_HPP

#include "aa_manifest.hpp"
#include "audio/channels.hpp"
#include <stdint.h>
#include <stddef.h>
#include <string>
#include <vector>

namespace nbt {

// The identity strings of Google's Desktop Head Unit.
SdrIdentity dhuIdentity();

// The manifest as Google's Desktop Head Unit advertises it, with `id` as its identity, plus a Bluetooth channel when
// id.btAdapterAddress is set. dhuIdentity() gives exactly the DHU's bytes (tests/dhu_sdp.h).
ServiceDiscovery referenceManifest(const SdrIdentity& id);

// The manifest a session sends: referenceManifest(id) with all the settings below applied.
ServiceDiscovery sessionManifest(const SdrIdentity& id);

// The manifest as ServiceDiscoveryResponse protobuf, without the message id.
std::vector<uint8_t> serializeManifest(const ServiceDiscovery& manifest);

// Corrects the sensor channel for a real car: drops the gyroscope and toll card, and has unleaded with no EV connector in
// place of the DHU's unleaded + electric and supercharger. This is what the phone is offered, not what it gets: of these,
// AASession sends only location, compass, car speed, night mode, driving status and GPS satellites, and only acknowledges the
// others.
// Returns true if anything was changed (prints the result).
bool configureSensors(ServiceDiscovery& manifest);
// The same with "Use Phone GPS" given (configureSensors(m) reads phoneGpsEnabled()): phoneGps leaves out every sensor
// that comes from the car's GPS / position (isCarPositionSensor: location, compass, GPS satellites, accelerometer,
// gyroscope), so the phone uses its own. For cars without the navigation antenna, which have no MOST GPS or heading.
bool configureSensors(ServiceDiscovery& manifest, bool phoneGps);
bool isCarPositionSensor(int sensor);

// The main display's VideoConfig. Environment (all optional):
//   NBT_VID_RES  = VideoResolution enum (1 = 800x480, 2 = 1280x720, 3 = 1920x1080); unset or invalid = 2. Decoder
//                  geometry, insets, DPI and touch coordinates use the same canvas (PR #66).
//   NBT_VID_FPS  = 30 or 60 (wire VIDEO_FPS_30 = 2 / VIDEO_FPS_60 = 1); unset keeps the manifest's 30 fps request.
//   NBT_VID_MW / NBT_VID_MH = margin width / height
//   NBT_VID_DPI, NBT_VID_INSETS = "top,bottom,left,right"
// Returns true if anything was changed (prints what it did).
bool configureMainVideo(ServiceDiscovery& manifest);

// ServiceDiscoveryResponse field 13 (session_configuration) bits: the head unit draws these itself, so the phone leaves
// them out of its UI. Read by the phone while the head unit asks for a protocol below 4.3 (we ask for 1.6); from 4.3 on
// it reads UiConfig field 5 instead. On a vertical rail they are the status block at its top (signal, battery, clock).
// Names as published (open-android-auto, AA 17.3); some phone versions are reported to ignore signal and battery.
enum { SESSION_HIDE_CLOCK = 1, SESSION_HIDE_PHONE_SIGNAL = 2, SESSION_HIDE_BATTERY_LEVEL = 4, SESSION_HIDE_ALL = 7 };

// Pure helper used by configureRailHide (host-tested): "clock,signal,battery" (any order; separators space , ; + |),
// "all", "none" / "off", or a number 0..7. Returns the bit mask, or -1 if anything is not understood.
int parseRailHide(const char* s);

// NBT_RAIL_HIDE: asks the phone not to draw the clock / phone signal / battery level, which gives the icons below them
// on the rail more room (at a higher NBT_VID_DPI the rail runs out of height and the mic slides under the 4th app icon).
// Unset: unchanged (0, the phone draws all three). Returns true if anything was changed (prints what it did).
bool configureRailHide(ServiceDiscovery& manifest);

// Declares the touchscreen (channel 7) at the video canvas size (1280x720 at 720p). NBT_TOUCH_CFG_H overrides its height.
bool configureTouchScreen(ServiceDiscovery& manifest);

// Removes whole channels by id. The list comes from NBT_SDR_DROP ("9", "8,9", "none" = keep everything); when unset
// and NBT_AUDIO_BT is on, the default is "9" (media sink only) so the phone plays media over Bluetooth A2DP.
// Car-verified 2026-10-23: the phone refuses the session without an audio sink ("No audio playback service") or
// without the mic ("No audio/mic"), so 8 and 14 must stay. DHU ids: 8 speech/guidance sink, 9 media sink, 14 mic.
// Returns the number of channels removed (prints what it did).
int dropSdrChannels(ServiceDiscovery& manifest);

// NBT_GUIDANCE_BT=1 (experiment, off by default): declare the channel 8 audio sink as SYSTEM_AUDIO instead of
// GUIDANCE. With no guidance sink (and no media sink in Bluetooth mode) the phone may mix nav prompts into its
// own audio, which already plays over Bluetooth A2DP. Never in browser audio mode (ch 8 is its guidance stream and
// ch 16 its SYSTEM_AUDIO sink): NBT_GUIDANCE_BT=1 is then ignored. Returns the number of sinks changed (0 or 1).
int retypeGuidanceSink(ServiceDiscovery& manifest);

// The audio sink channels (AA_CH_*_AUDIO, isAaAudioSinkChannel) are in audio/channels.hpp.

// Browser audio mode (or NBT_SYSTEM_SINK=1): adds a third audio sink on channel 16, PCM SYSTEM_AUDIO at
// 16 kHz / 16 bit / mono, so the phone sends notification chimes and UI sounds to the car instead of its own speaker.
// NBT_SYSTEM_SINK=0 leaves it out. Returns 1 if added. Does nothing if channel 16 already exists.
int addSystemAudioSink(ServiceDiscovery& manifest);

// NBT_AA_BROWSE=1 (experiment, off): adds the media browser service (channel 17, descriptor field 11) with an empty
// config, so the session logs whatever the phone sends there. Returns 1 if added.
int addMediaBrowserProbe(ServiceDiscovery& manifest);

// Keeps only the AudioConfigs with this sample rate in `channel`'s audio sink. Does nothing when no offer has that
// rate, or when it is the only rate offered. Returns the number of offers removed.
int keepAudioSinkRate(ServiceDiscovery& manifest, unsigned channel, uint32_t rate);

// Browser audio mode: the guidance sink (ch 8) offers only NBT_GUIDANCE_RATE (default 48000; 0 = keep every offer).
// The DHU template offers 16 and 48 kHz and the phone picks without asking (car 2026-11-01: 48 kHz), so one offer
// leaves no doubt about the rate. Other modes unchanged. Returns the number of offers removed.
int limitGuidanceOffers(ServiceDiscovery& manifest);

// The side the phone is told the driver sits on: NBT_DRIVER_SIDE = left or right. Unset or anything else is left, what the
// phone always heard before. Returns true if the manifest changed. Logs the side.
bool configureDriverPosition(ServiceDiscovery& manifest);

// Pure helper used by dropSdrChannels (host-tested): parses "8, 9,14" into ids (max 16).
size_t parseChannelList(const char* s, int* ids, size_t max_ids);

// The AudioConfigs the manifest offers for `channel`, in order. The phone picks one by its position in this list.
// Empty if the channel has no audio sink.
std::vector<AaAudioConfig> sdrAudioConfigs(const ServiceDiscovery& manifest, unsigned channel);

// AV START_INDICATION payload (after the 2-byte message id): { session = 1, configuration_index = 2 }. The index is
// the phone's choice among the channel's offered AudioConfigs (car 2026-11-01: guidance starts all said 1 = the
// second offer, 48 kHz, while our setup response had asked for config 0). Sets *session / *index (0xFFFFFFFF when
// absent) and returns true with *out filled if the index points into `offered`.
bool resolveAudioStart(const uint8_t* payload, size_t n, const std::vector<AaAudioConfig>& offered,
                       uint32_t* session, uint32_t* index, AaAudioConfig* out);

} // namespace nbt

#endif
