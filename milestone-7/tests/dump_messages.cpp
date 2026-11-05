// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Prints every protobuf message the daemon's code can build on its own, for tests/test_schema.py to decode:
//   MSG TAB <proto file> TAB <message type> TAB <label> TAB <body in hex>
// The marker tells these from the log lines the manifest code prints. The manifest follows the NBT_ settings in the
// environment, so the test runs this once per setting.
#include "aa_sensor_msg.hpp"
#include "audio/aa_media_msgs.hpp"
#include "kombi_nav_test_util.hpp"
#include "sdr_builder.hpp"
#include "wireless_setup.hpp"

#include <set>
#include <stdio.h>

using namespace nbt;
using namespace kntest;

static void show(const char* file, const char* type, const char* label, const std::vector<uint8_t>& message, size_t header) {
    printf("MSG\t%s\t%s\t%s\t", file, type, label);
    for (size_t i = header; i < message.size(); ++i) printf("%02x", message[i]);
    printf("\n");
}

static void manifests() {
    static const char* const kFile = "aa_manifest.proto";
    static const char* const kType = "nbt.manifest.ServiceDiscoveryResponse";
    show(kFile, kType, "reference manifest", serializeManifest(referenceManifest(dhuIdentity())), 0);
    SdrIdentity identity;
    identity.loadEnv();
    show(kFile, kType, "session manifest", serializeManifest(sessionManifest(identity)), 0);
}

static void media() {
    static const char* const kFile = "aa_session.proto";
    enum { MESSAGE_ID_BYTES = 2 };
    const uint64_t times[2] = { 150, 1793707200000000000ull };
    show(kFile, "nbt.session.MediaAck", "ack", aaMediaAck(3, 1, NULL, 0), MESSAGE_ID_BYTES);
    show(kFile, "nbt.session.MediaAck", "ack with times", aaMediaAck(300, 2, times, 2), MESSAGE_ID_BYTES);
    show(kFile, "nbt.session.AudioUnderflow", "underflow", aaUnderflowNotification(5), MESSAGE_ID_BYTES);
}

// The phone sends these, so there's no code of ours to build them: the bodies tests/test_media_msgs.cpp parses.
static void mediaPlayback() {
    static const char* const kFile = "aa_session.proto";
    static const uint8_t kStatus[] = { 0x08, 0x03, 0x12, 0x0B, 'A','p','p','l','e',' ','M','u','s','i','c', 0x18, 0x2A, 0x20, 0x01 };
    static const uint8_t kMetadata[] = {
        0x0A, 0x08, 'S','o','n','g',' ','O','n','e',
        0x12, 0x06, 'A','r','t','i','s','t',
        0x1A, 0x05, 'A','l','b','u','m',
        0x22, 0x03, 0xFF, 0xD8, 0xFF,
        0x2A, 0x08, 'M','y',' ','M','i','x',' ','1',
        0x30, 0xD2, 0x01
    };
    show(kFile, "nbt.session.MediaPlaybackStatus", "status", std::vector<uint8_t>(kStatus, kStatus + sizeof(kStatus)), 0);
    show(kFile, "nbt.session.MediaPlaybackMetadata", "metadata", std::vector<uint8_t>(kMetadata, kMetadata + sizeof(kMetadata)), 0);
}

static void sensors() {
    static const char* const kFile = "aa_session.proto";
    static const char* const kType = "nbt.session.SensorEvent";
    enum { MESSAGE_ID_BYTES = 2 };
    std::vector<uint8_t> event;
    LocationFix f;
    f.lat_e7 = 533553700; f.lon_e7 = -15262930; f.accuracy_e3 = 10000;
    buildLocationEvent(f, event);
    show(kFile, kType, "position", event, MESSAGE_ID_BYTES);
    f.has_altitude = f.has_speed = f.has_bearing = true;
    f.altitude_e2 = -2300; f.speed_e3 = 13889; f.bearing_e6 = 359999999;
    buildLocationEvent(f, event);
    show(kFile, kType, "position, all fields", event, MESSAGE_ID_BYTES);
    buildSatelliteEvent(0, event);
    show(kFile, kType, "no satellites", event, MESSAGE_ID_BYTES);
    buildSatelliteEvent(7, event);
    show(kFile, kType, "satellites", event, MESSAGE_ID_BYTES);
}

static void wireless() {
    static const char* const kFile = "aa_wireless.proto";
    enum { FRAME_HEADER_BYTES = 4 };
    ApInfo ap;
    ap.ssid = "NBT-AP";
    ap.key = "secret";
    ap.bssid = "AA:BB:CC:DD:EE:FF";
    show(kFile, "nbt.wireless.WifiStartRequest", "start request", wpp::buildStartRequest("192.168.1.1", 5277), FRAME_HEADER_BYTES);
    show(kFile, "nbt.wireless.WifiInfoResponse", "info response", wpp::buildInfoResponse(ap), FRAME_HEADER_BYTES);
}

static void showNav(const char* type, const char* label, const std::vector<uint8_t>& body) {
    show("aa_nav.proto", type, label, body, 0);
}

static void nav() {
    showNav("nbt.nav.NavigationStatus", "status", status(1));

    std::vector<std::pair<int, std::string> > next;
    next.push_back(std::make_pair(2, std::string("NEXT ST")));
    showNav("nbt.nav.NavigationState", "state", navState(7, "MAIN ST", -1, next));
    showNav("nbt.nav.NavigationState", "roundabout", navState(32, "RING RD", 90));
    for (int k = 0; k < 4; ++k) showNav("nbt.nav.NavigationState", "lanes", navStateLanes(7, "LANE RD", demoLanes(k)));

    showNav("nbt.nav.NavigationCurrentPosition", "position", curPos(100, "100", 1));
    showNav("nbt.nav.NavigationCurrentPosition", "no meters", curPos(100, "100", 1, 30, 5000, false));

    Bytes turn, distance;
    fStr(turn, 1, "ELM ST"); fInt(turn, 2, 1); fInt(turn, 3, 4); fInt(turn, 5, 2); fInt(turn, 6, 90);
    fInt(distance, 1, 300); fInt(distance, 2, 20); fInt(distance, 3, 200000); fInt(distance, 4, 6);
    showNav("nbt.nav.NextTurnEvent", "legacy turn", turn);
    showNav("nbt.nav.NextTurnDistanceEvent", "legacy distance", distance);

    // A whole scripted drive, each different message once.
    const std::vector<Tick> drive = demoDrive(demoRoute());
    std::set<std::vector<uint8_t> > seen;
    for (size_t i = 0; i < drive.size(); ++i) {
        if (seen.insert(drive[i].m.state).second) showNav("nbt.nav.NavigationState", "drive state", drive[i].m.state);
        if (seen.insert(drive[i].m.position).second) showNav("nbt.nav.NavigationCurrentPosition", "drive position", drive[i].m.position);
    }
}

int main() {
    manifests();
    media();
    mediaPlayback();
    sensors();
    wireless();
    nav();
    return 0;
}
