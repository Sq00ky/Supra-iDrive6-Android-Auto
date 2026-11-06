// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef AA_MANIFEST_HPP
#define AA_MANIFEST_HPP

// The Android Auto ServiceDiscoveryResponse as data. sdr_builder fills it in and writes it to the wire.
//
// Names come from Open Android Auto (reverse-engineered from the phone APK) and the Android Auto 17.9 APK's own enum
// strings. Their values match what Google's Desktop Head Unit sends. A value with no confirmed meaning stays visibly
// unnamed: "unknown" fields and *_DHU constants hold the number the DHU sends, not a claim about what it means.

#include "aa_keys.hpp"
#include "audio/channels.hpp"
#include <stdint.h>
#include <string>
#include <vector>

namespace nbt {

// Channels from the DHU manifest. The audio sinks (AA_CH_*_AUDIO) are in audio/channels.hpp.
enum AaChannelId {
    AA_CH_SENSORS = 1,
    AA_CH_CLUSTER_VIDEO = 2,
    AA_CH_CLUSTER_INPUT = 3,
    AA_CH_AUX_VIDEO = 4,
    AA_CH_AUX_INPUT = 5,
    AA_CH_MAIN_VIDEO = 6,
    AA_CH_MAIN_INPUT = 7,
    AA_CH_NAV_STATUS = 10,
    AA_CH_PHONE_STATUS = 11,
    AA_CH_MEDIA_PLAYBACK = 12,
    AA_CH_VENDOR_EXTENSION = 13,
    AA_CH_MICROPHONE = 14,
    AA_CH_BLUETOOTH = 15
};

enum AaSensorType {
    SENSOR_LOCATION = 1,
    SENSOR_COMPASS = 2,
    SENSOR_CAR_SPEED = 3,
    SENSOR_ODOMETER = 5,
    SENSOR_FUEL_LEVEL = 6,
    SENSOR_PARKING_BRAKE = 7,
    SENSOR_GEAR = 8,
    SENSOR_NIGHT_MODE = 10,
    SENSOR_DRIVING_STATUS = 13,
    SENSOR_ACCELEROMETER = 19,
    SENSOR_GYROSCOPE = 20,
    SENSOR_GPS_SATELLITE = 21,
    SENSOR_TOLL_CARD = 22
};

enum AaLocationCharacterization { LOCATION_RAW_GPS_ONLY = 256 };
enum AaFuelType { FUEL_UNLEADED = 1, FUEL_ELECTRIC = 10 };
enum AaEvConnector { EV_CONNECTOR_SUPERCHARGER = 8 };
enum AaDriverPosition { DRIVER_POSITION_LEFT = 0 };

enum AaMediaCodec { CODEC_AUDIO_PCM = 1, CODEC_VIDEO_H264 = 3 };
enum AaAudioType { AUDIO_TYPE_GUIDANCE = 1, AUDIO_TYPE_SYSTEM = 2, AUDIO_TYPE_MEDIA = 3 };

enum AaVideoResolution { VIDEO_800X480 = 1, VIDEO_1280X720 = 2, VIDEO_1920X1080 = 3 };
enum AaVideoFps { VIDEO_FPS_60 = 1, VIDEO_FPS_30 = 2 };
enum AaDisplayType { DISPLAY_MAIN = 0, DISPLAY_CLUSTER = 1, DISPLAY_AUXILIARY = 2 };
enum AaUiTheme { UI_THEME_AUTOMATIC = 0 };

// Values the DHU sends where the meaning is not confirmed.
enum { TOUCH_SCREEN_TYPE_DHU = 2, NAV_STATUS_TYPE_DHU = 1 };

// One AudioConfig an audio sink offers the phone.
struct AaAudioConfig {
    uint32_t sample_rate, bits, channels;
    AaAudioConfig() : sample_rate(0), bits(0), channels(0) {}
    AaAudioConfig(uint32_t rate, uint32_t depth, uint32_t count) : sample_rate(rate), bits(depth), channels(count) {}
};

struct SensorService {
    std::vector<int> sensors;
    int locationCharacterization;
    std::vector<int> fuelTypes;
    std::vector<int> evConnectors;
    SensorService() : locationCharacterization(0) {}
};

struct AudioSinkService {
    int codec;
    int audioType;
    std::vector<AaAudioConfig> configs;
    AudioSinkService() : codec(0), audioType(0) {}
};

struct VideoInsets {
    int top, bottom, left, right;
    VideoInsets() : top(0), bottom(0), left(0), right(0) {}
    VideoInsets(int t, int b, int l, int r) : top(t), bottom(b), left(l), right(r) {}
};

struct VideoConfig {
    int resolution, fps;
    int marginWidth, marginHeight;
    int dpi;
    int decoderAdditionalDepth;
    int viewingDistanceMm;
    int pixelAspectRatioE4;     // 10000 = 1.0
    int realDensity;
    int codec;
    VideoInsets insets;
    int uiTheme;
    VideoConfig() : resolution(0), fps(0), marginWidth(0), marginHeight(0), dpi(0), decoderAdditionalDepth(0),
                    viewingDistanceMm(0), pixelAspectRatioE4(0), realDensity(0), codec(0), uiTheme(0) {}
};

struct VideoSinkService {
    VideoConfig config;
    int displayId;
    int displayType;
    bool hasUnknown8;
    int unknown8;               // sent on the auxiliary display only, with the value of AAK_NAVIGATION
    VideoSinkService() : displayId(0), displayType(0), hasUnknown8(false), unknown8(0) {}
};

struct TouchScreenConfig {
    int width, height;
    int type;
    int unknown4;
    TouchScreenConfig() : width(0), height(0), type(0), unknown4(0) {}
};

struct TouchPadConfig {
    int width, height;
    int uiNavigation;
    int physicalWidthMm, physicalHeightMm;
    int uiAbsolute;
    int tapAsSelect;
    int sensitivity;
    TouchPadConfig() : width(0), height(0), uiNavigation(0), physicalWidthMm(0), physicalHeightMm(0), uiAbsolute(0),
                       tapAsSelect(0), sensitivity(0) {}
};

struct InputSourceService {
    std::vector<int> keycodes;
    bool hasTouchScreen;
    TouchScreenConfig touchScreen;
    bool hasTouchPad;
    TouchPadConfig touchPad;
    int displayId;
    InputSourceService() : hasTouchScreen(false), hasTouchPad(false), displayId(0) {}
};

struct MicrophoneService {
    int codec;
    AaAudioConfig config;
    MicrophoneService() : codec(0) {}
};

struct NavStatusService {
    int minimumIntervalMs;
    int type;
    int imageWidth, imageHeight;
    int imageColourDepthBits;
    NavStatusService() : minimumIntervalMs(0), type(0), imageWidth(0), imageHeight(0), imageColourDepthBits(0) {}
};

struct VendorExtensionService {
    std::string name;
    std::string package;
};

// One ChannelDescriptor. Only the member named by `service` is written.
struct AaChannel {
    enum Service {
        SENSOR_SOURCE, AUDIO_SINK, VIDEO_SINK, INPUT_SOURCE, MICROPHONE, BLUETOOTH, NAV_STATUS, MEDIA_PLAYBACK,
        PHONE_STATUS, VENDOR_EXTENSION
    };
    int id;
    Service service;
    SensorService sensors;
    AudioSinkService audioSink;
    VideoSinkService videoSink;
    InputSourceService input;
    MicrophoneService microphone;
    std::string bluetoothAddress;
    NavStatusService navStatus;
    VendorExtensionService vendorExtension;
    AaChannel(int channelId, Service kind) : id(channelId), service(kind) {}
};

// The strings the phone identifies the car by.
struct SdrIdentity {
    std::string carMake, carModel, carYear, vehicleId;
    std::string huMake, huModel, swBuild, swVersion;
    std::string btAdapterAddress;   // empty = no Bluetooth channel
    SdrIdentity();                  // Toyota / GR Supra defaults; BMW when carConfig().bmw
    void loadEnv();                 // NBT_CAR_MAKE, NBT_CAR_MODEL, NBT_CAR_YEAR, NBT_VEHICLE_ID,
                                    // NBT_HU_MAKE, NBT_HU_MODEL, NBT_SW_VERSION, NBT_BT_ADDR
};

// PingConfiguration. The DHU fills in four values whose individual meanings are not confirmed.
struct PingConfig {
    int unknown1, unknown2, unknown3, unknown4;
    PingConfig() : unknown1(0), unknown2(0), unknown3(0), unknown4(0) {}
};

struct ServiceDiscovery {
    std::vector<AaChannel> channels;
    SdrIdentity identity;
    int driverPosition;
    int sessionConfiguration;
    std::string displayName;
    int probeForSupport;
    PingConfig ping;
    std::vector<AaChannel> trailingChannels;   // channels written after the other fields, in the order they were added
    ServiceDiscovery() : driverPosition(0), sessionConfiguration(0), probeForSupport(0) {}
};

} // namespace nbt

#endif
