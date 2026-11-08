// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#include "sdr_builder.hpp"
#include "aa_proto.hpp"
#include "nbt_env.hpp"
#include "audio/config.hpp"
#include "car_config.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

namespace nbt {

SdrIdentity::SdrIdentity()
    : carMake("Toyota"), carModel("GR Supra"), carYear("2020"),
      vehicleId("6f1c2a40-5a1e-4c7e-9e21-0a90bd51e0a7"),
      huMake("Toyota"), huModel("NBT Evo ID6"), swBuild("nbt-aad"), swVersion("1.3.33") {
    // BMW group cars (carplay.ini logo / Manufacturer, or NBT_VEHICLE=bmw|mini|rollsroyce): the phone shows the
    // make on its exit icon. Own vehicle id per make so the phone keeps separate per-car settings.
    // NBT_CAR_* / NBT_HU_* still override.
    const CarConfig& cc = carConfig();
    if (cc.make != MAKE_TOYOTA) {
        carMake = carMakeName(cc.make);
        huMake = carMakeName(cc.make);
        carYear = "2018";
        vehicleId = cc.make == MAKE_MINI ? "8d4f1b26-3e7a-4c95-a0b2-6c1e9f73d548"
                  : cc.make == MAKE_ROLLS_ROYCE ? "c27a9e14-6b3d-4f81-9d5c-2e8b7a40f619"
                  : "3b9e7d52-1c4f-4a8e-b6d0-5f2a9c81e4b3";
        // carplay.ini Model is "<chassis>-NBTEvo-<date>", e.g. F20-NBTEvo-0716 -> "F20"
        std::string m = cc.model;
        size_t dash = m.find('-');
        carModel = (dash != std::string::npos && dash > 0) ? m.substr(0, dash) : (m.empty() ? std::string("iDrive") : m);
    }
}

static void envStr(const char* name, std::string& out) {
    const char* v = getenv(name);
    if (v && *v) out = v;
}

void SdrIdentity::loadEnv() {
    envStr("NBT_CAR_MAKE", carMake);   envStr("NBT_CAR_MODEL", carModel);
    envStr("NBT_CAR_YEAR", carYear);   envStr("NBT_VEHICLE_ID", vehicleId);
    envStr("NBT_HU_MAKE", huMake);     envStr("NBT_HU_MODEL", huModel);
    envStr("NBT_SW_VERSION", swVersion);
    envStr("NBT_BT_ADDR", btAdapterAddress);
}

SdrIdentity dhuIdentity() {
    SdrIdentity id;
    id.carMake = "Google";
    id.carModel = "Desktop Head Unit";
    id.carYear = "2015";
    id.vehicleId = "0582521f-87ac-40ed-8a99-9659ba2eb7f3";
    id.huMake = "Google";
    id.huModel = "Desktop Head Unit";
    id.swBuild = "2022-12-15-495540972";
    id.swVersion = "2.1-windows";
    return id;
}

namespace {

// ---- the reference manifest -------------------------------------------------------------------------------------

template <size_t N>
std::vector<int> listOf(const int (&values)[N]) {
    return std::vector<int>(values, values + N);
}

AaChannel sensorChannel() {
    static const int kSensors[] = {
        SENSOR_LOCATION, SENSOR_COMPASS, SENSOR_CAR_SPEED, SENSOR_ODOMETER, SENSOR_FUEL_LEVEL, SENSOR_PARKING_BRAKE,
        SENSOR_GEAR, SENSOR_NIGHT_MODE, SENSOR_DRIVING_STATUS, SENSOR_ACCELEROMETER, SENSOR_GYROSCOPE,
        SENSOR_GPS_SATELLITE, SENSOR_TOLL_CARD
    };
    static const int kFuelTypes[] = { FUEL_UNLEADED, FUEL_ELECTRIC };
    static const int kEvConnectors[] = { EV_CONNECTOR_SUPERCHARGER };
    AaChannel c(AA_CH_SENSORS, AaChannel::SENSOR_SOURCE);
    c.sensors.sensors = listOf(kSensors);
    c.sensors.locationCharacterization = LOCATION_RAW_GPS_ONLY;
    c.sensors.fuelTypes = listOf(kFuelTypes);
    c.sensors.evConnectors = listOf(kEvConnectors);
    return c;
}

AaChannel videoChannel(int id, int resolution, int marginWidth, int marginHeight, const VideoInsets& insets,
                       int displayId, int displayType) {
    enum { DPI = 160, VIEWING_DISTANCE_MM = 500, PIXEL_ASPECT_RATIO_1_TO_1 = 10000 };
    AaChannel c(id, AaChannel::VIDEO_SINK);
    VideoConfig& v = c.videoSink.config;
    v.resolution = resolution;
    v.fps = VIDEO_FPS_30;
    v.marginWidth = marginWidth;
    v.marginHeight = marginHeight;
    v.dpi = DPI;
    v.decoderAdditionalDepth = 0;
    v.viewingDistanceMm = VIEWING_DISTANCE_MM;
    v.pixelAspectRatioE4 = PIXEL_ASPECT_RATIO_1_TO_1;
    v.realDensity = DPI;
    v.codec = CODEC_VIDEO_H264;
    v.insets = insets;
    v.uiTheme = UI_THEME_AUTOMATIC;
    c.videoSink.displayId = displayId;
    c.videoSink.displayType = displayType;
    return c;
}

enum { DISPLAY_ID_MAIN = 0, DISPLAY_ID_AUX = 1, DISPLAY_ID_CLUSTER = 2 };

AaChannel keysOnlyInputChannel(int id, int displayId) {
    AaChannel c(id, AaChannel::INPUT_SOURCE);
    c.input.displayId = displayId;
    return c;
}

AaChannel mainInputChannel() {
    static const int kKeycodes[] = {
        AAK_SOFT_LEFT, AAK_SOFT_RIGHT, AAK_HOME, AAK_BACK, AAK_CALL, AAK_ENDCALL,
        AAK_0, AAK_1, AAK_2, AAK_3, AAK_4, AAK_5, AAK_6, AAK_7, AAK_8, AAK_9,
        AAK_STAR, AAK_POUND,
        AAK_DPAD_UP, AAK_DPAD_DOWN, AAK_DPAD_LEFT, AAK_DPAD_RIGHT, AAK_DPAD_CENTER,
        AAK_PLUS, AAK_SEARCH,
        AAK_MEDIA_PLAY_PAUSE, AAK_MEDIA_NEXT, AAK_MEDIA_PREVIOUS, AAK_MEDIA_PLAY, AAK_MEDIA_PAUSE,
        AAK_DPAD_UP_LEFT, AAK_DPAD_DOWN_LEFT, AAK_DPAD_UP_RIGHT, AAK_DPAD_DOWN_RIGHT,
        AAK_ROTARY_CONTROLLER, AAK_MEDIA, AAK_NAVIGATION, AAK_TEL,
        AAK_UNKNOWN_65541, AAK_UNKNOWN_65542, AAK_UNKNOWN_65543
    };
    AaChannel c(AA_CH_MAIN_INPUT, AaChannel::INPUT_SOURCE);
    c.input.keycodes = listOf(kKeycodes);
    c.input.hasTouchScreen = true;
    c.input.touchScreen.width = 1280;
    c.input.touchScreen.height = 480;
    c.input.touchScreen.type = TOUCH_SCREEN_TYPE_DHU;
    c.input.hasTouchPad = true;
    c.input.touchPad.width = 240;
    c.input.touchPad.height = 240;
    c.input.touchPad.uiNavigation = 1;
    c.input.touchPad.physicalWidthMm = 55;
    c.input.touchPad.physicalHeightMm = 55;
    c.input.touchPad.uiAbsolute = 0;
    c.input.touchPad.tapAsSelect = 1;
    c.input.touchPad.sensitivity = 5;
    c.input.displayId = DISPLAY_ID_MAIN;
    return c;
}

AaChannel audioSinkChannel(int id, int audioType) {
    AaChannel c(id, AaChannel::AUDIO_SINK);
    c.audioSink.codec = CODEC_AUDIO_PCM;
    c.audioSink.audioType = audioType;
    return c;
}

AaChannel navStatusChannel() {
    AaChannel c(AA_CH_NAV_STATUS, AaChannel::NAV_STATUS);
    c.navStatus.minimumIntervalMs = 500;
    c.navStatus.type = NAV_STATUS_TYPE_DHU;
    c.navStatus.imageWidth = 100;
    c.navStatus.imageHeight = 100;
    c.navStatus.imageColourDepthBits = 16;
    return c;
}

// ---- protobuf ---------------------------------------------------------------------------------------------------

using proto::ProtoWriter;

ProtoWriter encode(const AaAudioConfig& c) {
    enum { F_SAMPLE_RATE = 1, F_BIT_DEPTH = 2, F_CHANNEL_COUNT = 3 };
    ProtoWriter w;
    w.unsignedInteger(F_SAMPLE_RATE, c.sample_rate);
    w.unsignedInteger(F_BIT_DEPTH, c.bits);
    w.unsignedInteger(F_CHANNEL_COUNT, c.channels);
    return w;
}

ProtoWriter encode(const SensorService& s) {
    enum { F_SENSOR = 1, F_LOCATION_CHARACTERIZATION = 2, F_FUEL_TYPE = 3, F_EV_CONNECTOR = 4 };
    enum { F_SENSOR_TYPE = 1 };
    ProtoWriter w;
    for (size_t i = 0; i < s.sensors.size(); ++i) {
        ProtoWriter sensor;
        sensor.integer(F_SENSOR_TYPE, s.sensors[i]);
        w.message(F_SENSOR, sensor);
    }
    w.integer(F_LOCATION_CHARACTERIZATION, s.locationCharacterization);
    w.repeated(F_FUEL_TYPE, s.fuelTypes);
    w.repeated(F_EV_CONNECTOR, s.evConnectors);
    return w;
}

ProtoWriter encode(const AudioSinkService& s) {
    enum { F_CODEC = 1, F_AUDIO_TYPE = 2, F_AUDIO_CONFIG = 3 };
    ProtoWriter w;
    w.integer(F_CODEC, s.codec);
    w.integer(F_AUDIO_TYPE, s.audioType);
    for (size_t i = 0; i < s.configs.size(); ++i) w.message(F_AUDIO_CONFIG, encode(s.configs[i]));
    return w;
}

ProtoWriter encode(const VideoInsets& i) {
    enum { F_TOP = 1, F_BOTTOM = 2, F_LEFT = 3, F_RIGHT = 4 };
    ProtoWriter w;
    w.integer(F_TOP, i.top);
    w.integer(F_BOTTOM, i.bottom);
    w.integer(F_LEFT, i.left);
    w.integer(F_RIGHT, i.right);
    return w;
}

ProtoWriter encode(const VideoConfig& v) {
    enum {
        F_RESOLUTION = 1, F_FPS = 2, F_MARGIN_WIDTH = 3, F_MARGIN_HEIGHT = 4, F_DPI = 5, F_DECODER_ADDITIONAL_DEPTH = 6,
        F_VIEWING_DISTANCE = 7, F_PIXEL_ASPECT_RATIO = 8, F_REAL_DENSITY = 9, F_CODEC = 10, F_ADDITIONAL_CONFIG = 11
    };
    enum { F_DISPLAY_INSETS = 1, F_ADDITIONAL_INSETS_2 = 2, F_ADDITIONAL_INSETS_3 = 3, F_THEME = 4 };
    ProtoWriter additional;
    additional.message(F_DISPLAY_INSETS, encode(v.insets));
    additional.message(F_ADDITIONAL_INSETS_2, ProtoWriter());
    additional.message(F_ADDITIONAL_INSETS_3, ProtoWriter());
    additional.integer(F_THEME, v.uiTheme);
    ProtoWriter w;
    w.integer(F_RESOLUTION, v.resolution);
    w.integer(F_FPS, v.fps);
    w.integer(F_MARGIN_WIDTH, v.marginWidth);
    w.integer(F_MARGIN_HEIGHT, v.marginHeight);
    w.integer(F_DPI, v.dpi);
    w.integer(F_DECODER_ADDITIONAL_DEPTH, v.decoderAdditionalDepth);
    w.integer(F_VIEWING_DISTANCE, v.viewingDistanceMm);
    w.integer(F_PIXEL_ASPECT_RATIO, v.pixelAspectRatioE4);
    w.integer(F_REAL_DENSITY, v.realDensity);
    w.integer(F_CODEC, v.codec);
    w.message(F_ADDITIONAL_CONFIG, additional);
    return w;
}

ProtoWriter encode(const VideoSinkService& s) {
    enum { F_VIDEO_CONFIG = 4, F_DISPLAY_ID = 6, F_DISPLAY_TYPE = 7, F_UNKNOWN_8 = 8 };
    ProtoWriter w;
    w.message(F_VIDEO_CONFIG, encode(s.config));
    w.integer(F_DISPLAY_ID, s.displayId);
    w.integer(F_DISPLAY_TYPE, s.displayType);
    if (s.hasUnknown8) w.integer(F_UNKNOWN_8, s.unknown8);
    return w;
}

ProtoWriter encode(const TouchScreenConfig& t) {
    enum { F_WIDTH = 1, F_HEIGHT = 2, F_TYPE = 3, F_UNKNOWN_4 = 4 };
    ProtoWriter w;
    w.integer(F_WIDTH, t.width);
    w.integer(F_HEIGHT, t.height);
    w.integer(F_TYPE, t.type);
    w.integer(F_UNKNOWN_4, t.unknown4);
    return w;
}

ProtoWriter encode(const TouchPadConfig& t) {
    enum {
        F_WIDTH = 1, F_HEIGHT = 2, F_UI_NAVIGATION = 3, F_PHYSICAL_WIDTH = 4, F_PHYSICAL_HEIGHT = 5, F_UI_ABSOLUTE = 6,
        F_TAP_AS_SELECT = 7, F_SENSITIVITY = 8
    };
    ProtoWriter w;
    w.integer(F_WIDTH, t.width);
    w.integer(F_HEIGHT, t.height);
    w.integer(F_UI_NAVIGATION, t.uiNavigation);
    w.integer(F_PHYSICAL_WIDTH, t.physicalWidthMm);
    w.integer(F_PHYSICAL_HEIGHT, t.physicalHeightMm);
    w.integer(F_UI_ABSOLUTE, t.uiAbsolute);
    w.integer(F_TAP_AS_SELECT, t.tapAsSelect);
    w.integer(F_SENSITIVITY, t.sensitivity);
    return w;
}

ProtoWriter encode(const InputSourceService& s) {
    enum { F_KEYCODES = 1, F_TOUCH_SCREEN = 2, F_TOUCH_PAD = 3, F_DISPLAY_ID = 5 };
    ProtoWriter w;
    if (!s.keycodes.empty()) w.packed(F_KEYCODES, s.keycodes);
    if (s.hasTouchScreen) w.message(F_TOUCH_SCREEN, encode(s.touchScreen));
    if (s.hasTouchPad) w.message(F_TOUCH_PAD, encode(s.touchPad));
    w.integer(F_DISPLAY_ID, s.displayId);
    return w;
}

ProtoWriter encode(const MicrophoneService& s) {
    enum { F_CODEC = 1, F_AUDIO_CONFIG = 2 };
    ProtoWriter w;
    w.integer(F_CODEC, s.codec);
    w.message(F_AUDIO_CONFIG, encode(s.config));
    return w;
}

ProtoWriter encode(const NavStatusService& s) {
    enum { F_MINIMUM_INTERVAL = 1, F_TYPE = 2, F_IMAGE_OPTIONS = 3 };
    enum { F_IMAGE_WIDTH = 1, F_IMAGE_HEIGHT = 2, F_IMAGE_COLOUR_DEPTH = 3 };
    ProtoWriter image;
    image.integer(F_IMAGE_WIDTH, s.imageWidth);
    image.integer(F_IMAGE_HEIGHT, s.imageHeight);
    image.integer(F_IMAGE_COLOUR_DEPTH, s.imageColourDepthBits);
    ProtoWriter w;
    w.integer(F_MINIMUM_INTERVAL, s.minimumIntervalMs);
    w.integer(F_TYPE, s.type);
    w.message(F_IMAGE_OPTIONS, image);
    return w;
}

ProtoWriter encode(const VendorExtensionService& s) {
    enum { F_NAME = 1, F_PACKAGE = 2 };
    ProtoWriter w;
    w.string(F_NAME, s.name);
    w.string(F_PACKAGE, s.package);
    return w;
}

ProtoWriter encode(const AaChannel& c) {
    enum {
        F_ID = 1, F_SENSOR_SOURCE = 2, F_MEDIA_SINK = 3, F_INPUT_SOURCE = 4, F_MICROPHONE = 5, F_BLUETOOTH = 6,
        F_NAV_STATUS = 8, F_MEDIA_PLAYBACK = 9, F_PHONE_STATUS = 10, F_VENDOR_EXTENSION = 12
    };
    enum { F_BLUETOOTH_ADDRESS = 1 };
    ProtoWriter w;
    w.integer(F_ID, c.id);
    switch (c.service) {
    case AaChannel::SENSOR_SOURCE:    w.message(F_SENSOR_SOURCE, encode(c.sensors)); break;
    case AaChannel::AUDIO_SINK:       w.message(F_MEDIA_SINK, encode(c.audioSink)); break;
    case AaChannel::VIDEO_SINK:       w.message(F_MEDIA_SINK, encode(c.videoSink)); break;
    case AaChannel::INPUT_SOURCE:     w.message(F_INPUT_SOURCE, encode(c.input)); break;
    case AaChannel::MICROPHONE:       w.message(F_MICROPHONE, encode(c.microphone)); break;
    case AaChannel::NAV_STATUS:       w.message(F_NAV_STATUS, encode(c.navStatus)); break;
    case AaChannel::MEDIA_PLAYBACK:   w.message(F_MEDIA_PLAYBACK, ProtoWriter()); break;
    case AaChannel::PHONE_STATUS:     w.message(F_PHONE_STATUS, ProtoWriter()); break;
    case AaChannel::VENDOR_EXTENSION: w.message(F_VENDOR_EXTENSION, encode(c.vendorExtension)); break;
    case AaChannel::BLUETOOTH: {
        ProtoWriter bluetooth;
        bluetooth.string(F_BLUETOOTH_ADDRESS, c.bluetoothAddress);
        w.message(F_BLUETOOTH, bluetooth);
        break;
    }
    }
    return w;
}

ProtoWriter encode(const PingConfig& p) {
    enum { F_UNKNOWN_1 = 1, F_UNKNOWN_2 = 2, F_UNKNOWN_3 = 3, F_UNKNOWN_4 = 4 };
    ProtoWriter w;
    w.integer(F_UNKNOWN_1, p.unknown1);
    w.integer(F_UNKNOWN_2, p.unknown2);
    w.integer(F_UNKNOWN_3, p.unknown3);
    w.integer(F_UNKNOWN_4, p.unknown4);
    return w;
}

// HeadUnitInfo carries copies of the identity strings.
ProtoWriter encodeHeadUnitInfo(const SdrIdentity& id) {
    enum {
        F_CAR_MAKE = 1, F_CAR_MODEL = 2, F_CAR_YEAR = 3, F_VEHICLE_ID = 4, F_HU_MAKE = 5, F_HU_MODEL = 6, F_SW_BUILD = 7,
        F_SW_VERSION = 8
    };
    ProtoWriter w;
    w.string(F_CAR_MAKE, id.carMake);
    w.string(F_CAR_MODEL, id.carModel);
    w.string(F_CAR_YEAR, id.carYear);
    w.string(F_VEHICLE_ID, id.vehicleId);
    w.string(F_HU_MAKE, id.huMake);
    w.string(F_HU_MODEL, id.huModel);
    w.string(F_SW_BUILD, id.swBuild);
    w.string(F_SW_VERSION, id.swVersion);
    return w;
}

AaChannel* findChannel(std::vector<AaChannel>& channels, unsigned id) {
    for (size_t i = 0; i < channels.size(); ++i)
        if ((unsigned)channels[i].id == id) return &channels[i];
    return NULL;
}

AaChannel* findChannel(ServiceDiscovery& m, unsigned id) {
    AaChannel* c = findChannel(m.channels, id);
    return c ? c : findChannel(m.trailingChannels, id);
}

} // namespace

ServiceDiscovery referenceManifest(const SdrIdentity& id) {
    static const int kClusterKeycodes[] = { AAK_DPAD_CENTER, AAK_ROTARY_CONTROLLER };
    enum { CLUSTER_VIDEO_MARGIN_WIDTH = 400, CLUSTER_VIDEO_MARGIN_HEIGHT = 80, AUX_VIDEO_MARGIN_WIDTH = 442,
           MAIN_VIDEO_MARGIN_WIDTH = 1280, MAIN_VIDEO_MARGIN_HEIGHT = 480 };

    ServiceDiscovery m;
    m.identity = id;
    m.driverPosition = DRIVER_POSITION_LEFT;
    m.ping.unknown1 = 3000;
    m.ping.unknown2 = 1000;
    m.ping.unknown3 = 200;
    m.ping.unknown4 = 5;

    m.channels.push_back(sensorChannel());
    m.channels.push_back(videoChannel(AA_CH_CLUSTER_VIDEO, VIDEO_800X480, CLUSTER_VIDEO_MARGIN_WIDTH,
                                      CLUSTER_VIDEO_MARGIN_HEIGHT, VideoInsets(40, 40, 200, 200),
                                      DISPLAY_ID_CLUSTER, DISPLAY_CLUSTER));
    AaChannel clusterInput = keysOnlyInputChannel(AA_CH_CLUSTER_INPUT, DISPLAY_ID_CLUSTER);
    clusterInput.input.keycodes = listOf(kClusterKeycodes);
    m.channels.push_back(clusterInput);
    AaChannel auxVideo = videoChannel(AA_CH_AUX_VIDEO, VIDEO_800X480, AUX_VIDEO_MARGIN_WIDTH, 0,
                                      VideoInsets(0, 0, 221, 221), DISPLAY_ID_AUX, DISPLAY_AUXILIARY);
    auxVideo.videoSink.hasUnknown8 = true;
    auxVideo.videoSink.unknown8 = AAK_NAVIGATION;
    m.channels.push_back(auxVideo);
    m.channels.push_back(keysOnlyInputChannel(AA_CH_AUX_INPUT, DISPLAY_ID_AUX));
    m.channels.push_back(videoChannel(AA_CH_MAIN_VIDEO, VIDEO_1280X720, MAIN_VIDEO_MARGIN_WIDTH,
                                      MAIN_VIDEO_MARGIN_HEIGHT, VideoInsets(), DISPLAY_ID_MAIN, DISPLAY_MAIN));
    m.channels.push_back(mainInputChannel());

    AaChannel guidance = audioSinkChannel(AA_CH_GUIDANCE_AUDIO, AUDIO_TYPE_GUIDANCE);
    guidance.audioSink.configs.push_back(AaAudioConfig(16000, 16, 1));
    guidance.audioSink.configs.push_back(AaAudioConfig(48000, 16, 1));
    m.channels.push_back(guidance);
    AaChannel media = audioSinkChannel(AA_CH_MEDIA_AUDIO, AUDIO_TYPE_MEDIA);
    media.audioSink.configs.push_back(AaAudioConfig(48000, 16, 2));
    m.channels.push_back(media);

    m.channels.push_back(navStatusChannel());
    m.channels.push_back(AaChannel(AA_CH_PHONE_STATUS, AaChannel::PHONE_STATUS));
    m.channels.push_back(AaChannel(AA_CH_MEDIA_PLAYBACK, AaChannel::MEDIA_PLAYBACK));
    AaChannel vendor(AA_CH_VENDOR_EXTENSION, AaChannel::VENDOR_EXTENSION);
    vendor.vendorExtension.name = "com.example.android.projection.protocol.EchoVendorExtension";
    vendor.vendorExtension.package = "com.example.echovendor";
    m.channels.push_back(vendor);
    AaChannel microphone(AA_CH_MICROPHONE, AaChannel::MICROPHONE);
    microphone.microphone.codec = CODEC_AUDIO_PCM;
    microphone.microphone.config = AaAudioConfig(16000, 16, 1);
    m.channels.push_back(microphone);

    if (!id.btAdapterAddress.empty()) {
        AaChannel bluetooth(AA_CH_BLUETOOTH, AaChannel::BLUETOOTH);
        bluetooth.bluetoothAddress = id.btAdapterAddress;
        m.trailingChannels.push_back(bluetooth);
    }
    return m;
}

bool configureDriverPosition(ServiceDiscovery& m) {
    const int was = m.driverPosition;
    const char* side = getenv("NBT_DRIVER_SIDE");
    if (side && strcmp(side, "right") == 0) {
        m.driverPosition = DRIVER_POSITION_RIGHT;
        printf("[SDR] driver's side: right (NBT_DRIVER_SIDE)\n");
    } else if (side && strcmp(side, "left") == 0) {
        m.driverPosition = DRIVER_POSITION_LEFT;
        printf("[SDR] driver's side: left (NBT_DRIVER_SIDE)\n");
    } else {
        if (side && *side) printf("[SDR] NBT_DRIVER_SIDE='%s' ignored: use left or right.\n", side);
        m.driverPosition = DRIVER_POSITION_LEFT;
        printf("[SDR] driver's side: left (default)\n");
    }
    return m.driverPosition != was;
}

ServiceDiscovery sessionManifest(const SdrIdentity& id) {
    ServiceDiscovery m = referenceManifest(id);
    configureDriverPosition(m);
    configureSensors(m);
    configureMainVideo(m);
    configureRailHide(m);
    configureTouchScreen(m);
    dropSdrChannels(m);
    retypeGuidanceSink(m);
    addSystemAudioSink(m);
    limitGuidanceOffers(m);
    return m;
}

std::vector<uint8_t> serializeManifest(const ServiceDiscovery& m) {
    enum {
        F_CHANNEL = 1, F_CAR_MAKE = 2, F_CAR_MODEL = 3, F_CAR_YEAR = 4, F_VEHICLE_ID = 5, F_DRIVER_POSITION = 6,
        F_HU_MAKE = 7, F_HU_MODEL = 8, F_SW_BUILD = 9, F_SW_VERSION = 10, F_SESSION_CONFIGURATION = 13,
        F_DISPLAY_NAME = 14, F_PROBE_FOR_SUPPORT = 15, F_CONNECTION_CONFIGURATION = 16, F_HEAD_UNIT_INFO = 17
    };
    enum { F_PING_CONFIGURATION = 1 };
    ProtoWriter w;
    for (size_t i = 0; i < m.channels.size(); ++i) w.message(F_CHANNEL, encode(m.channels[i]));
    w.string(F_CAR_MAKE, m.identity.carMake);
    w.string(F_CAR_MODEL, m.identity.carModel);
    w.string(F_CAR_YEAR, m.identity.carYear);
    w.string(F_VEHICLE_ID, m.identity.vehicleId);
    w.integer(F_DRIVER_POSITION, m.driverPosition);
    w.string(F_HU_MAKE, m.identity.huMake);
    w.string(F_HU_MODEL, m.identity.huModel);
    w.string(F_SW_BUILD, m.identity.swBuild);
    w.string(F_SW_VERSION, m.identity.swVersion);
    w.integer(F_SESSION_CONFIGURATION, m.sessionConfiguration);
    w.string(F_DISPLAY_NAME, m.displayName);
    w.integer(F_PROBE_FOR_SUPPORT, m.probeForSupport);
    ProtoWriter connection;
    connection.message(F_PING_CONFIGURATION, encode(m.ping));
    w.message(F_CONNECTION_CONFIGURATION, connection);
    w.message(F_HEAD_UNIT_INFO, encodeHeadUnitInfo(m.identity));
    for (size_t i = 0; i < m.trailingChannels.size(); ++i) w.message(F_CHANNEL, encode(m.trailingChannels[i]));
    return w.bytes();
}

size_t parseChannelList(const char* s, int* ids, size_t max_ids) {
    size_t n = 0;
    if (!s) return 0;
    while (*s && n < max_ids) {
        while (*s == ' ' || *s == ',' || *s == ';') ++s;
        if (!*s) break;
        char* end = NULL;
        long v = strtol(s, &end, 0);
        if (end == s) break;
        if (v >= 0 && v < 256) ids[n++] = (int)v;
        s = end;
    }
    return n;
}

namespace {

int dropListedChannels(std::vector<AaChannel>& channels, const int* ids, size_t nids, const char* list) {
    int removed = 0;
    for (size_t i = 0; i < channels.size();) {
        bool drop = false;
        for (size_t k = 0; k < nids; ++k) if (ids[k] == channels[i].id) drop = true;
        if (drop) {
            printf("[SDR] dropped channel %d (NBT_SDR_DROP=%s)\n", channels[i].id, list);
            channels.erase(channels.begin() + i);
            ++removed;
        } else {
            ++i;
        }
    }
    return removed;
}

} // namespace

int dropSdrChannels(ServiceDiscovery& m) {
    const char* list = getenv("NBT_SDR_DROP");
    if (list && (strcmp(list, "none") == 0 || strcmp(list, "0") == 0)) return 0;
    if (!list || !*list) {
        if (!nbt::btAudioEnabled()) return 0;
        list = "9";   // car-verified 2026-10-23: keep speech (8) + mic (14), the phone requires both
    }
    int ids[16];
    size_t nids = parseChannelList(list, ids, 16);
    if (nids == 0) return 0;
    return dropListedChannels(m.channels, ids, nids, list) + dropListedChannels(m.trailingChannels, ids, nids, list);
}

int addSystemAudioSink(ServiceDiscovery& m) {
    int f = nbt::envFlag("NBT_SYSTEM_SINK");
    bool on = (f < 0) ? nbt::browserAudioEnabled() : (f == 1);
    if (!on || findChannel(m, AA_CH_SYSTEM_AUDIO)) return 0;
    AaChannel sink = audioSinkChannel(AA_CH_SYSTEM_AUDIO, AUDIO_TYPE_SYSTEM);
    sink.audioSink.configs.push_back(AaAudioConfig(16000, 16, 1));
    m.trailingChannels.push_back(sink);
    printf("[SDR] added channel %d: SYSTEM_AUDIO sink, 16 kHz mono (NBT_SYSTEM_SINK=0 leaves it out)\n",
           AA_CH_SYSTEM_AUDIO);
    return 1;
}

int keepAudioSinkRate(ServiceDiscovery& m, unsigned channel, uint32_t rate) {
    AaChannel* c = findChannel(m, channel);
    if (!c || c->service != AaChannel::AUDIO_SINK || rate == 0) return 0;
    std::vector<AaAudioConfig> kept;
    const std::vector<AaAudioConfig>& offers = c->audioSink.configs;
    for (size_t i = 0; i < offers.size(); ++i)
        if (offers[i].sample_rate == rate) kept.push_back(offers[i]);
    const int removed = (int)(offers.size() - kept.size());
    if (removed == 0 || kept.empty()) return 0;
    c->audioSink.configs = kept;
    return removed;
}

int limitGuidanceOffers(ServiceDiscovery& m) {
    if (!nbt::browserAudioEnabled()) return 0;
    const long rate = nbt::envRange("NBT_GUIDANCE_RATE", 48000, 0, 192000);
    if (rate == 0) return 0;
    const int removed = keepAudioSinkRate(m, AA_CH_GUIDANCE_AUDIO, (uint32_t)rate);
    if (removed)
        printf("[SDR] channel %d guidance sink: offering %ld Hz only (%d other offer%s removed; NBT_GUIDANCE_RATE=0 "
               "keeps all)\n", AA_CH_GUIDANCE_AUDIO, rate, removed, removed == 1 ? "" : "s");
    return removed;
}

int retypeGuidanceSink(ServiceDiscovery& m) {
    // Default ON in Bluetooth audio mode since v1.6.9 (car-verified nbt-aad-21: nav prompts play over BT A2DP).
    // NBT_GUIDANCE_BT=0 restores the GUIDANCE sink, =1 forces it on outside BT mode.
    // Unset follows Bluetooth-audio mode. 0/off/no/false keeps GUIDANCE. 1/on/yes/true forces SYSTEM_AUDIO.
    int g = nbt::envFlag("NBT_GUIDANCE_BT");
    if (nbt::browserAudioEnabled()) {
        // Browser mode plays ch 8 as the guidance stream (own ducking, own rate offers) and adds the SYSTEM_AUDIO sink
        // on ch 16: retyping ch 8 would give the phone two SYSTEM_AUDIO sinks. A leftover NBT_GUIDANCE_BT=1 is ignored.
        if (g == 1)
            printf("[SDR] NBT_GUIDANCE_BT=1 ignored in browser audio mode: channel 8 stays GUIDANCE\n");
        return 0;
    }
    bool on = (g < 0) ? nbt::btAudioEnabled() : (g == 1);
    if (!on) return 0;
    AaChannel* c = findChannel(m, AA_CH_GUIDANCE_AUDIO);
    if (!c || c->service != AaChannel::AUDIO_SINK || c->audioSink.audioType != AUDIO_TYPE_GUIDANCE) return 0;
    c->audioSink.audioType = AUDIO_TYPE_SYSTEM;
    printf("[SDR] channel 8 audio sink: GUIDANCE -> SYSTEM_AUDIO (nav prompts over Bluetooth; NBT_GUIDANCE_BT=0 disables)\n");
    return 1;
}

namespace {

bool envInt(const char* n, int& v) {
    const char* e = getenv(n);
    if (!e || !*e) return false;
    v = atoi(e);
    return true;
}

} // namespace

bool isCarPositionSensor(int sensor) {
    return sensor == SENSOR_LOCATION || sensor == SENSOR_COMPASS || sensor == SENSOR_GPS_SATELLITE ||
           sensor == SENSOR_ACCELEROMETER || sensor == SENSOR_GYROSCOPE;
}

bool configureSensors(ServiceDiscovery& m) { return configureSensors(m, phoneGpsEnabled()); }

bool configureSensors(ServiceDiscovery& m, bool phoneGps) {
    static const int kSensors[] = {
        SENSOR_LOCATION, SENSOR_COMPASS, SENSOR_CAR_SPEED, SENSOR_ODOMETER, SENSOR_FUEL_LEVEL, SENSOR_PARKING_BRAKE,
        SENSOR_GEAR, SENSOR_NIGHT_MODE, SENSOR_DRIVING_STATUS, SENSOR_ACCELEROMETER, SENSOR_GPS_SATELLITE
    };
    static const int kFuelTypes[] = { FUEL_UNLEADED };
    AaChannel* c = findChannel(m, AA_CH_SENSORS);
    if (!c || c->service != AaChannel::SENSOR_SOURCE) return false;
    const std::vector<int> all = listOf(kSensors), fuelTypes = listOf(kFuelTypes);
    std::vector<int> sensors;
    for (size_t i = 0; i < all.size(); ++i)
        if (!phoneGps || !isCarPositionSensor(all[i])) sensors.push_back(all[i]);
    if (c->sensors.sensors == sensors && c->sensors.fuelTypes == fuelTypes && c->sensors.evConnectors.empty())
        return false;
    c->sensors.sensors = sensors;
    c->sensors.fuelTypes = fuelTypes;
    c->sensors.evConnectors.clear();
    printf("[SDR] sensors advertised: ");
    for (size_t i = 0; i < sensors.size(); ++i) printf("%s%d", i ? "," : "", sensors[i]);
    printf("; fuel: unleaded; EV connectors: none%s\n",
           phoneGps ? " (Use Phone GPS: location, compass, GPS satellites and accelerometer are left to the phone)" : "");
    return true;
}

bool configureMainVideo(ServiceDiscovery& m) {
    int vmw = 0, vmh = 0, vdpi;
    // NBT_VID_RES (PR #66): the canvas the phone renders (VIDEO_800X480 / VIDEO_1280X720 default / VIDEO_1920X1080);
    // invalid values = 720p. Decoder size, insets, DPI and touch follow it (nbt_env.hpp videoCanvas*).
    const char* resEnv = getenv("NBT_VID_RES");
    const bool res = resEnv && *resEnv;
    const int vres = videoResolution();
    // NBT_VID_FPS (PR #66): 30 -> VIDEO_FPS_30, 60 -> VIDEO_FPS_60; unset keeps the manifest's VIDEO_FPS_30.
    const unsigned fpsSet = videoFpsSetting();
    const bool fps = fpsSet != 0;
    const int vfps = fpsSet == 60 ? VIDEO_FPS_60 : VIDEO_FPS_30;
    // The DHU's legacy margins (1280 x 480) only fit the 720p canvas: the others get 0 unless NBT_VID_MW / NBT_VID_MH.
    bool mw = vres != VIDEO_1280X720, mh = mw;
    if (envInt("NBT_VID_MW", vmw)) mw = true;
    if (envInt("NBT_VID_MH", vmh)) mh = true;
    // Default DPI 140 (car-verified 2026-10-23, w9: rail icons no longer overlap in crop mode), scaled with the canvas
    // width so 480p / 1080p keep the same layout (88 / 210). NBT_VID_DPI=<n> overrides, NBT_VID_DPI=0 keeps the
    // phone's own DPI. MINI round display (1180_480 coding): DPI 152 and 40 px side insets at 720p (car-verified on a
    // MINI F60), scaled the same way.
    const bool mini = miniRoundPanel();
    vdpi = ((mini ? 152 : 140) * videoCanvasWidth() + 640) / 1280;
    bool dpi = true;
    if (envInt("NBT_VID_DPI", vdpi) && vdpi <= 0) { dpi = false; vdpi = 0; }
    // Default (car-verified in w7 on the 1280x480 panel): the phone keeps its UI inside the part of its canvas the
    // panel can show and we show it scaled evenly: 720p on 1280x480 = the top 480 rows 1:1 (bottom inset 240), 480p =
    // the top 800x300 at x1.6 (0,180,0,0). Other panels (NBT_SCREEN / carplay.ini) get matching insets; taller ones
    // fill the height and reserve the hidden columns on the right. NBT_VID_NATIVE=1 shows the canvas 1:1 and adds a
    // right inset for the columns beyond the panel (1180x480 at 720p -> 0,240,0,100).
    // NBT_VID_INSETS=off restores the plain manifest.
    const int side = mini ? 40 * videoCanvasWidth() / 1280 : 0;
    VideoInsets insets(0, videoCanvasHeight() - canvasRowsShown(), side, videoCanvasWidth() - canvasColsShown() + side);
    bool hasInsets = true;
    const char* ins = getenv("NBT_VID_INSETS");
    if (ins && *ins) {
        hasInsets = false;
        insets = VideoInsets();
        if (strcmp(ins, "off") != 0) {
            int t = 0, b = 0, l = 0, r = 0;
            if (sscanf(ins, "%d,%d,%d,%d", &t, &b, &l, &r) >= 2) { hasInsets = true; insets = VideoInsets(t, b, l, r); }
        }
    }
    if (!res && !fps && !mw && !mh && !hasInsets && !dpi) return false;
    AaChannel* c = findChannel(m, AA_CH_MAIN_VIDEO);
    if (!c || c->service != AaChannel::VIDEO_SINK) return false;
    VideoConfig& v = c->videoSink.config;
    if (res) v.resolution = vres;
    if (fps) v.fps = vfps;
    if (mw) v.marginWidth = vmw;
    if (mh) v.marginHeight = vmh;
    if (dpi) { v.dpi = vdpi; v.realDensity = vdpi; }
    if (hasInsets) v.insets = insets;
    if (mini) printf("[SDR] MINI round display (carplay.ini 1180_480): default DPI %d, side insets %d px (NBT_VID_DPI / NBT_VID_INSETS override)\n",
                     ((152 * videoCanvasWidth() + 640) / 1280), side);
    printf("[SDR] main VideoConfig override: res=%s%d fps=%s%d (%u fps asked, decoder %u) mw=%s%d mh=%s%d dpi=%s%d insets=%s(%d,%d,%d,%d)\n",
           res ? "" : "(keep)", vres, fps ? "" : "(keep)", fps ? vfps : (int)v.fps, videoRequestedFps(), videoDecoderFps(),
           mw ? "" : "(keep)", vmw, mh ? "" : "(keep)", vmh,
           dpi ? "" : "(keep)", vdpi, hasInsets ? "" : "(keep)", insets.top, insets.bottom, insets.left, insets.right);
    return true;
}

int parseRailHide(const char* s) {
    if (!s) return 0;
    while (*s == ' ') ++s;
    if (!*s) return 0;
    if (*s >= '0' && *s <= '9') {
        char* end = NULL;
        const long v = strtol(s, &end, 0);
        while (end && *end == ' ') ++end;
        return (end && !*end && v >= 0 && v <= SESSION_HIDE_ALL) ? (int)v : -1;
    }
    int mask = 0;
    while (*s) {
        while (*s == ' ' || *s == ',' || *s == ';' || *s == '+' || *s == '|') ++s;
        if (!*s) break;
        char w[16];
        size_t n = 0;
        while (*s && *s != ' ' && *s != ',' && *s != ';' && *s != '+' && *s != '|') {
            if (n + 1 >= sizeof(w)) return -1;
            w[n++] = (char)tolower((unsigned char)*s++);
        }
        w[n] = 0;
        if (strcmp(w, "clock") == 0) mask |= SESSION_HIDE_CLOCK;
        else if (strcmp(w, "signal") == 0) mask |= SESSION_HIDE_PHONE_SIGNAL;
        else if (strcmp(w, "battery") == 0) mask |= SESSION_HIDE_BATTERY_LEVEL;
        else if (strcmp(w, "all") == 0) mask |= SESSION_HIDE_ALL;
        else if (strcmp(w, "none") != 0 && strcmp(w, "off") != 0) return -1;
    }
    return mask;
}

bool configureRailHide(ServiceDiscovery& m) {
    const char* s = getenv("NBT_RAIL_HIDE");
    if (!s || !*s) return false;
    const int mask = parseRailHide(s);
    if (mask < 0) {
        printf("[SDR] NBT_RAIL_HIDE='%s' ignored (clock, signal, battery, all or none)\n", s);
        return false;
    }
    const int before = m.sessionConfiguration;
    m.sessionConfiguration = (m.sessionConfiguration & ~SESSION_HIDE_ALL) | mask;
    printf("[SDR] NBT_RAIL_HIDE=%s: session_configuration %d, the phone is asked not to draw: clock %s, signal %s, "
           "battery %s\n", s, m.sessionConfiguration, (mask & SESSION_HIDE_CLOCK) ? "yes" : "no",
           (mask & SESSION_HIDE_PHONE_SIGNAL) ? "yes" : "no", (mask & SESSION_HIDE_BATTERY_LEVEL) ? "yes" : "no");
    return m.sessionConfiguration != before;
}

// Touchscreen geometry the phone is told about: the whole video canvas, hidden rows included (PR #66), so 1280x720 at
// 720p (the DHU declares 1280x480) and 800x480 at 480p; the forwarder maps panel pixels onto it (touchScale()).
// NBT_TOUCH_CFG_H overrides the height.
bool configureTouchScreen(ServiceDiscovery& m) {
    int w = videoCanvasWidth(), h = videoCanvasHeight();
    envInt("NBT_TOUCH_CFG_H", h);
    AaChannel* c = findChannel(m, AA_CH_MAIN_INPUT);
    if (!c || c->service != AaChannel::INPUT_SOURCE || !c->input.hasTouchScreen ||
        (c->input.touchScreen.width == w && c->input.touchScreen.height == h))
        return false;
    c->input.touchScreen.width = w;
    c->input.touchScreen.height = h;
    printf("[SDR] touchscreen declared %dx%d\n", w, h);
    return true;
}

std::vector<AaAudioConfig> sdrAudioConfigs(const ServiceDiscovery& m, unsigned channel) {
    for (size_t i = 0; i < m.channels.size() + m.trailingChannels.size(); ++i) {
        const AaChannel& c = i < m.channels.size() ? m.channels[i] : m.trailingChannels[i - m.channels.size()];
        if ((unsigned)c.id == channel && c.service == AaChannel::AUDIO_SINK) return c.audioSink.configs;
    }
    return std::vector<AaAudioConfig>();
}

namespace {

// AV START_INDICATION: { session = 1, configuration_index = 2 }.
void readAudioStart(const uint8_t* p, size_t n, uint32_t& session, uint32_t& index) {
    enum { F_SESSION = 1, F_CONFIGURATION_INDEX = 2 };
    proto::PbReader r(p, n);
    proto::PbField f;
    while (r.next(f)) {
        if (f.wire != proto::WIRE_VARINT) continue;
        if (f.num == F_SESSION) session = (uint32_t)f.value;
        else if (f.num == F_CONFIGURATION_INDEX) index = (uint32_t)f.value;
    }
}

} // namespace

bool resolveAudioStart(const uint8_t* payload, size_t n, const std::vector<AaAudioConfig>& offered,
                       uint32_t* session, uint32_t* index, AaAudioConfig* out) {
    uint32_t s = 0xFFFFFFFFu, i = 0xFFFFFFFFu;
    if (payload && n) readAudioStart(payload, n, s, i);
    if (session) *session = s;
    if (index) *index = i;
    if (i == 0xFFFFFFFFu || i >= offered.size()) return false;
    if (out) *out = offered[i];
    return true;
}

} // namespace nbt
