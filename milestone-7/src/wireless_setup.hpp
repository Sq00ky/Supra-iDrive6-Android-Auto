// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef WIRELESS_SETUP_HPP
#define WIRELESS_SETUP_HPP

#include <stdint.h>
#include <stddef.h>
#include <string>
#include <vector>

namespace nbt {

// Access-point description sent to the phone (WifiInfoResponse).
struct ApInfo {
    std::string ssid;
    std::string key;
    std::string bssid;   // "AA:BB:CC:DD:EE:FF" (uppercase, colon separated)
    std::string ip;      // head unit IP the phone must connect to
    int security;        // 8 = WPA2_PERSONAL
    int apType;          // 1 = DYNAMIC
    ApInfo() : security(8), apType(1) {}
};

// Wireless Android Auto Wi-Fi Projection Protocol (WPP) over RFCOMM.
// Frame: [u16 payload_len BE][u16 msg_id BE][protobuf payload]
namespace wpp {

enum MsgId {
    kWifiStartRequest  = 1,  // HU -> phone  {ip=1 string, port=2 int32}
    kWifiInfoRequest   = 2,  // phone -> HU  (empty)
    kWifiInfoResponse  = 3,  // HU -> phone  {ssid=1,key=2,bssid=3,security=4,ap_type=5}
    kWifiVersionRequest  = 4, // HU -> phone (openauto sends it empty)
    kWifiVersionResponse = 5, // phone -> HU
    kWifiConnectStatus = 6,  // phone -> HU  {status=1}
    kWifiStartResponse = 7   // phone -> HU  {ip=2,port=3,status=4}
};

// Protobuf helpers (proto2 wire format, only what WPP needs)
void putVarint(std::vector<uint8_t>& out, uint64_t v);
void putString(std::vector<uint8_t>& out, int field, const std::string& s);
void putInt(std::vector<uint8_t>& out, int field, uint64_t v);
bool getVarint(const uint8_t* p, size_t n, size_t& pos, uint64_t& v);
// Finds a varint field (returns false if absent). Skips other fields.
bool findVarintField(const uint8_t* p, size_t n, int field, uint64_t& v);

std::vector<uint8_t> frame(uint16_t id, const std::vector<uint8_t>& payload);
std::vector<uint8_t> buildStartRequest(const std::string& ip, uint16_t port);
std::vector<uint8_t> buildInfoResponse(const ApInfo& ap);

} // namespace wpp

// Reads the [WiFi] section (SSID=, Passphrase=, BSSID=, Channel=) of the CarPlay
// iAP2 config (e.g. /fs/sda1/opt/conn/carplay/iap2_Wifi.cfg). Lines starting
// with ';' or '#' are ignored. Returns true if ssid and passphrase were found.
bool loadCarPlayApConfig(const char* path, ApInfo& out, bool quiet = false);

// One SSID/passphrase pair the phone may be told, and the file it came from.
struct ApCandidate {
    ApInfo ap;
    std::string path;
};

// The car keeps the AP SSID/passphrase in two iAP2 configs. Connectivity keeps iap2.cfg in sync with the live AP
// on every AP config update but leaves the SSID/passphrase in iap2_Wifi.cfg alone unless name/channel/version
// change, so iap2_Wifi.cfg can hold a stale password (BMW tester 2026-10-28). Reads the files in `paths` order
// (kApCfgPaths: iap2.cfg first) and returns the distinct SSID/passphrase pairs, the SSID of the first readable
// file paired with every passphrase found. Never logs a passphrase.
std::vector<ApCandidate> collectApCandidates(const char* const* paths, size_t nPaths);

// Default search order: iap2.cfg (always in sync), then iap2_Wifi.cfg as a fallback.
extern const char* const kApCfgPaths[];
extern const size_t kApCfgPathCount;

// Fills ip / bssid from `ifconfig <ifname>` output when still empty.
// Env overrides: NBT_AP_IP, NBT_AP_BSSID.
void fillApNetworkInfo(const char* ifname, ApInfo& ap);

// Blocking handshake on an already-open RFCOMM stream (fd). Returns true when the
// phone reports WifiConnectStatus == 0 (SUCCESS). timeout_ms bounds each read wait.
//   mode 0 = legacy (WifiStartRequest, then answer WifiInfoRequest)           -- what WAAD does
//   mode 1 = version-first (empty WifiVersionRequest, then WifiStartRequest)  -- what openauto does
//   mode 2 = proactive (WifiStartRequest, then WifiInfoResponse without waiting for the request)
bool runWirelessHandshake(int fd, const ApInfo& ap, uint16_t port, int timeout_ms, int mode = 0);

// After a successful handshake keep the RFCOMM link open and serviced: answers WifiPingRequest, repeats
// the AP info if the phone asks again, logs everything else. Waits up to timeout_ms for data.
// Returns false once the link is closed by the phone / broken.
bool wppService(int fd, int timeout_ms, const ApInfo& ap, uint16_t port);

// True when the bytes look like BMW Connected / MyBMW BCL (startup magic 12 34 56 78, Etch 0xDEADBEEF, the
// 0x1337 watchdog or a self-consistent BCL frame), i.e. the SPP link is the BMW app's, not Android Auto's.
bool looksLikeBmwBcl(const uint8_t* p, size_t n);
// True when the last handshake / service call stopped because the link was BMW Connected.
bool lastLinkForeign();
// True when the peer of the last link sent at least one well-formed Android Auto wireless frame (on a working
// phone: WifiStartResponse 00 02 00 07 .. right after our WifiStartRequest).
bool lastLinkSpokeWpp();
// Listen this long on a fresh link before writing (0 = write at once). BMW/MINI default 300 ms: a MyBMW link that
// already sent its BCL startup magic gets nothing written into it.
void setSppPeekMs(int ms);

} // namespace nbt

#endif // WIRELESS_SETUP_HPP
