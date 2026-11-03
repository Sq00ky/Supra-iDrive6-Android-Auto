// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef ALT_WIFI_HPP
#define ALT_WIFI_HPP

// Wi-Fi Direct as an alternative to the CarPlay hotspot (NBT_ALT_WIFI).
//
// Cars without the CarPlay licence have no /var/opt/conn/carplay/iap2.cfg, so their iap2_Wifi.cfg carries only a
// factory template and the daemon cannot offer a phone any Wi-Fi at all. Those cars do still run the Marvell
// Wi-Fi Direct group `wfd0` when Miracast is coded (`wifi, mira`): Connectivity starts it at boot
// (NetworkingService_wfd0.autogo = yes), SSID "DIRECT-03-" + VIN, group owner 172.16.222.1, DHCP .100-.110.
//
// Its WPA2 passphrase is in /var/opt/conn/WFDPass.conf as 20 hex chars: the passphrase XORed with the hard-coded
// key below, then hex. Reverse-engineered from Connectivity's CWifiSecurity (encDecyrptPsk, 0x68e6d0):
//     out[i] = in[i] ^ key[i % strlen(key)]
// The file is written by CWifiSecurity::writeFile with a random 10-char passphrase (createRandomPsk), so it is a
// real WPA2 key and not derived from the VIN or the coding. WFDWps.conf holds the 8-digit WPS PIN the same way.
// Phone test 2026-11-03: the raw hex is refused, so the file is decoded before it is offered. The key and the
// decoded values are the owner's own car credentials; nothing here is transmitted anywhere.
//
// Header-only so the host test (tests/test_alt_wifi.cpp) runs without the QNX build.
#include "wireless_setup.hpp"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

namespace nbt {

// Which Wi-Fi the daemon offers the phone.
//   CARPLAY (default, unset) - the CarPlay hotspot only: today's behaviour, unchanged for every car that works
//   DIRECT                   - the Wi-Fi Direct group only, and no fallback: one path per run, so a car log shows
//                              exactly what was tried
//   BOTH                     - Wi-Fi Direct first, and the CarPlay hotspot once the phone has taken the DIRECT
//                              details twice without connecting
enum AltWifiMode { ALT_WIFI_CARPLAY = 0, ALT_WIFI_DIRECT = 1, ALT_WIFI_BOTH = 2 };

inline AltWifiMode parseAltWifi(const char* v) {
    if (!v || !*v) return ALT_WIFI_BOTH;      // default since v1.8.78: hotspot if it exists, else the DIRECT group
    char buf[16];
    size_t n = 0;
    for (; v[n] && n < sizeof(buf) - 1; ++n) buf[n] = (char)toupper((unsigned char)v[n]);
    buf[n] = 0;
    // trim spaces around the word so " NBT_ALT_WIFI=direct " still works
    size_t a = 0, b = n;
    while (a < b && (buf[a] == ' ' || buf[a] == '\t')) ++a;
    while (b > a && (buf[b - 1] == ' ' || buf[b - 1] == '\t')) --b;
    buf[b] = 0;
    const char* w = buf + a;
    if (strcmp(w, "DIRECT") == 0 || strcmp(w, "WFD") == 0 || strcmp(w, "P2P") == 0) return ALT_WIFI_DIRECT;
    if (strcmp(w, "BOTH") == 0) return ALT_WIFI_BOTH;
    if (strcmp(w, "CARPLAY") == 0 || strcmp(w, "UAP") == 0) return ALT_WIFI_CARPLAY;
    return ALT_WIFI_BOTH;         // an unknown word gets the default (hotspot first, DIRECT behind it)
}

inline AltWifiMode altWifiMode() { return parseAltWifi(getenv("NBT_ALT_WIFI")); }

// Which source is offered first when both are available. CARPLAY and BOTH prefer the car's CarPlay hotspot
// (the path that is car-verified wherever it exists); only DIRECT puts the Wi-Fi Direct group first.
inline bool altWifiPrefersCarplay() { return altWifiMode() != ALT_WIFI_DIRECT; }

inline const char* altWifiName(AltWifiMode m) {
    return m == ALT_WIFI_DIRECT ? "DIRECT" : (m == ALT_WIFI_BOTH ? "BOTH" : "CARPLAY");
}

// CWifiSecurity's hard-coded key (Connectivity, CHBString "NBTEvo0715").
inline const char* wfdSecretKey() { return "NBTEvo0715"; }

inline std::string wfdTrim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (unsigned char)s[a] <= ' ') ++a;
    while (b > a && (unsigned char)s[b - 1] <= ' ') --b;
    return s.substr(a, b - a);
}

// File text (hex, CRLF or not) -> decoded secret. False + why for anything that is not clean hex.
inline bool decodeWfdSecret(const std::string& fileText, std::string& out, std::string& why) {
    out.clear();
    std::string hex;
    for (size_t i = 0; i < fileText.size(); ++i) {
        unsigned char c = (unsigned char)fileText[i];
        if (c <= ' ') continue;                 // CR/LF/space/tab
        if (!isxdigit(c)) { why = "not a hex file"; return false; }
        hex.push_back((char)c);
    }
    if (hex.empty()) { why = "empty file"; return false; }
    if (hex.size() % 2) { why = "odd number of hex digits"; return false; }
    const char* key = wfdSecretKey();
    const size_t klen = strlen(key);
    for (size_t i = 0; i < hex.size(); i += 2) {
        const char hi = (char)toupper((unsigned char)hex[i]), lo = (char)toupper((unsigned char)hex[i + 1]);
        unsigned v = (unsigned)((hi <= '9' ? hi - '0' : hi - 'A' + 10) << 4);
        v |= (unsigned)(lo <= '9' ? lo - '0' : lo - 'A' + 10);
        out.push_back((char)(v ^ (unsigned char)key[(i / 2) % klen]));
    }
    why.clear();
    return true;
}

// WPA2 passphrase rule: 8..63 printable ASCII (the driver's "8...63 ASCII or 64 HEX" limit).
inline bool wfdPassphraseOk(const std::string& p) {
    if (p.size() < 8 || p.size() > 63) return false;
    for (size_t i = 0; i < p.size(); ++i) {
        unsigned char c = (unsigned char)p[i];
        if (c < 0x20 || c > 0x7e) return false;
    }
    return true;
}

inline bool wfdPinOk(const std::string& p) {
    if (p.size() != 8) return false;
    for (size_t i = 0; i < p.size(); ++i) if (!isdigit((unsigned char)p[i])) return false;
    return true;
}

// Connectivity writes both files to /var/opt/conn (same directory the car's own copy of wifidir_init.conf uses),
// and /fs/sda1 holds the persistent copy like the CarPlay configs.
inline const char* const* wfdPassPaths() {
    static const char* const p[] = { "/var/opt/conn/WFDPass.conf", "/fs/sda1/opt/conn/WFDPass.conf" };
    return p;
}
inline size_t wfdPassPathCount() { return 2; }

inline const char* const* wfdWpsPaths() {
    static const char* const p[] = { "/var/opt/conn/WFDWps.conf", "/fs/sda1/opt/conn/WFDWps.conf" };
    return p;
}
inline size_t wfdWpsPathCount() { return 2; }

inline bool readWholeFile(const char* path, std::string& out) {
    out.clear();
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    char buf[512];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    fclose(f);
    return true;
}

// Reads the first readable WFDPass.conf in `paths` and decodes it. Logs what it found (this is the owner's own
// car and the log line is how the passphrase can be read out and typed on a phone by hand).
inline bool loadWfdAp(const char* const* paths, size_t nPaths, ApInfo& ap, std::string& pathUsed,
                        bool quiet = false) {
    pathUsed.clear();
    std::string why;
    for (size_t i = 0; i < nPaths; ++i) {
        std::string text;
        if (!readWholeFile(paths[i], text)) continue;
        std::string pass;
        if (!decodeWfdSecret(text, pass, why)) {
            if (!quiet) printf("[Wireless] Wi-Fi Direct passphrase file %s: %s (ignored)\n", paths[i], why.c_str());
            continue;
        }
        if (!wfdPassphraseOk(pass)) {
            if (!quiet)
                printf("[Wireless] Wi-Fi Direct passphrase file %s: decoded %u bytes, not a WPA2 passphrase "
                       "(8..63 printable ASCII; ignored)\n", paths[i], (unsigned)pass.size());
            continue;
        }
        ap.key = pass;
        ap.security = 8;         // WPA2_PERSONAL
        ap.apType = 1;           // DYNAMIC
        pathUsed = paths[i];
        if (!quiet)
            printf("[Wireless] Wi-Fi Direct AP: passphrase from %s: '%s' (decoded, %u chars; type it on the phone's "
                   "Wi-Fi settings to join the DIRECT group by hand)\n",
                   paths[i], pass.c_str(), (unsigned)pass.size());
        return true;
    }
    return false;
}

// Log-only: the WPS PIN of the same group, decoded the same way.
inline bool loadWfdWpsPin(const char* const* paths, size_t nPaths, std::string& pin, std::string& pathUsed) {
    pathUsed.clear();
    std::string why;
    for (size_t i = 0; i < nPaths; ++i) {
        std::string text;
        if (!readWholeFile(paths[i], text)) continue;
        std::string v;
        if (!decodeWfdSecret(text, v, why) || !wfdPinOk(v)) continue;
        pin = v;
        pathUsed = paths[i];
        printf("[Wireless] Wi-Fi Direct WPS PIN from %s: %s (WPS push-button / PIN pairing; not needed for a normal "
               "Wi-Fi join)\n", paths[i], pin.c_str());
        return true;
    }
    return false;
}

// ---- SSID ------------------------------------------------------------------------------------------
// The group's name is "DIRECT-03-" + VIN (CWlanWiFiDirectCfg::startUp), but the VIN is not something this daemon
// has: read the name the group is actually running with instead. Three sources, cheapest first.
inline bool parseIfconfigSsid(const std::string& text, std::string& ssid) {
    size_t pos = 0;
    while (pos < text.size()) {
        size_t e = text.find('\n', pos);
        if (e == std::string::npos) e = text.size();
        std::string line = wfdTrim(text.substr(pos, e - pos));
        pos = e + 1;
        if (line.empty() || line[0] == '#') continue;
        size_t i = 0;
        while (i < line.size() && line[i] != ':' && line[i] != ' ' && line[i] != '\t') ++i;
        std::string k = line.substr(0, i);
        for (size_t x = 0; x < k.size(); ++x) k[x] = (char)tolower((unsigned char)k[x]);
        if (k != "ssid" && k != "essid") continue;
        std::string v = wfdTrim(line.substr(i));
        if (!v.empty() && v[0] == ':') v = wfdTrim(v.substr(1));
        if (v.size() > 2 && v[0] == '"' && v[v.size() - 1] == '"') v = v.substr(1, v.size() - 2);
        if (v.empty()) continue;
        ssid = v;
        return true;
    }
    return false;
}

// mwud replies with newline-separated key=value lines, and its own state dumps use the same keys with a colon
// ("WIFIDIR: go_ssid: DIRECT-...", "Current SSID:%s", "SSID: %s"). Every separator in the line is tried, so both
// forms are read, and every key mwud's strings show for the group name.
inline bool mwuSsidKey(const std::string& k) {
    return k == "ssid" || k == "go_ssid" || k == "group_ssid" || k == "groupssid" || k == "current_ssid";
}

inline bool parseMwuSsid(const std::string& body, std::string& ssid) {
    size_t pos = 0;
    while (pos < body.size()) {
        size_t e = body.find('\n', pos);
        if (e == std::string::npos) e = body.size();
        std::string line = wfdTrim(body.substr(pos, e - pos));
        pos = e + 1;
        size_t sep = line.find_first_of("=:");
        while (sep != std::string::npos) {
            std::string k = wfdTrim(line.substr(0, sep));
            size_t sp = k.find_last_of(" \t");        // "WIFIDIR: go_ssid: X" -> "go_ssid"
            if (sp != std::string::npos) k = wfdTrim(k.substr(sp + 1));
            for (size_t x = 0; x < k.size(); ++x) k[x] = (char)tolower((unsigned char)k[x]);
            if (mwuSsidKey(k)) {
                std::string v = wfdTrim(line.substr(sep + 1));
                while (!v.empty() && (v[0] == ':' || v[0] == '=')) v = wfdTrim(v.substr(1));
                if (v.size() > 2 && v[0] == '"' && v[v.size() - 1] == '"') v = v.substr(1, v.size() - 2);
                if (!v.empty()) {
                    ssid = v;
                    return true;
                }
            }
            sep = line.find_first_of("=:", sep + 1);
        }
    }
    return false;
}

// The interface's own IPv4 address (`ifconfig <if>` -> "inet 172.16.222.1 netmask ..."). Pure parser first so the
// host test can drive it; the popen wrapper is only used when a bind to the configured address failed.
inline bool parseIfconfigIpv4(const std::string& text, std::string& ip) {
    size_t pos = 0;
    while (pos < text.size()) {
        size_t e = text.find('\n', pos);
        if (e == std::string::npos) e = text.size();
        std::string line = wfdTrim(text.substr(pos, e - pos));
        pos = e + 1;
        if (line.compare(0, 5, "inet ") != 0 && line.compare(0, 5, "inet\t") != 0) continue;
        std::string v = wfdTrim(line.substr(5));
        size_t sp = v.find_first_of(" \t");
        if (sp != std::string::npos) v = v.substr(0, sp);
        if (v.empty() || v.compare(0, 7, "0.0.0.0") == 0) continue;
        ip = v;
        return true;
    }
    return false;
}

inline bool interfaceIpv4(const char* ifname, std::string& ip) {
    if (!ifname || !*ifname) return false;
    char cmd[96];
    snprintf(cmd, sizeof(cmd), "/sbin/ifconfig %s", ifname);
    FILE* p = popen(cmd, "r");
    if (!p) return false;
    std::string text;
    char line[512];
    while (fgets(line, sizeof(line), p)) text += line;
    pclose(p);
    return parseIfconfigIpv4(text, ip);
}

// Same /24? (host order, so it does not matter how the caller got the numbers)
inline bool wfdSameSubnet(unsigned a, unsigned b) {
    return (a & 0xffffff00u) == (b & 0xffffff00u);
}

inline bool wfdSameSubnetStr(const std::string& a, const std::string& b) {
    struct in_addr x, y;
    if (!inet_aton(a.c_str(), &x) || !inet_aton(b.c_str(), &y)) return false;
    return wfdSameSubnet(ntohl(x.s_addr), ntohl(y.s_addr));
}

// ---- listener address ------------------------------------------------------------------------------
// Where the daemon listens for the phone, which has to be the interface the phone was told about: on
// 2026-11-03 the phone joined the DIRECT group and connected to nothing, because the listener was bound to
// the CarPlay AP (172.16.224.1) while the handshake offered wfd0 (172.16.222.1).
// NBT_AP_IP still pins one address. BOTH gets NULL (bind every interface) because its fallback lives on the
// other subnet, and the accept filter below keeps strangers out.
inline const char* wirelessListenIp() {
    const char* e = getenv("NBT_AP_IP");
    if (e && *e) return e;
    AltWifiMode m = altWifiMode();
    if (m == ALT_WIFI_DIRECT) {
        const char* w = getenv("NBT_WFD_IP");
        return (w && *w) ? w : "172.16.222.1";
    }
    if (m == ALT_WIFI_BOTH) return NULL;
    return "172.16.224.1";
}

// Accept only a phone that is on the same subnet as the address the connection arrived on (host order, from
// getsockname on the accepted socket). This needs no constants: whatever the car numbers wfd0 or uap0, the phone
// has to be inside that network, and a client on Ethernet/USB/OBD is refused. Loopback is never a phone.
inline bool wirelessPeerAllowed(unsigned peerHostOrder, unsigned localHostOrder) {
    if ((localHostOrder & 0xff000000u) == 0x7f000000u) return false;        // 127/8
    if ((peerHostOrder & 0xff000000u) == 0x7f000000u) return false;
    if ((localHostOrder & 0xffffffffu) == 0) return false;                  // 0.0.0.0: nothing to compare with
    return wfdSameSubnet(peerHostOrder, localHostOrder);
}

// Same /24 as one of the cached Android Auto interface addresses (uap0 / wfd0, host order). Used when the
// listener is bound to every interface (BOTH), where the arrival address alone would also admit a client on the
// head unit's other networks. Falls back to the arrival-network rule when no interface could be read.
inline bool wfdPeerInNets(unsigned peerHostOrder, const unsigned* nets, size_t count, unsigned localHostOrder) {
    if (count == 0) return wirelessPeerAllowed(peerHostOrder, localHostOrder);
    for (size_t i = 0; i < count; ++i)
        if (wfdSameSubnet(peerHostOrder, nets[i])) return true;
    return false;
}


// ---- group name derivation -------------------------------------------------------------------------
// The group is "DIRECT-03-" + Connectivity's WLAN device name with the whitespace removed
// (CWlanWiFiDirectCfg::startUp: SSID = "DIRECT-03-" + removeWhiteSpaces(NetworkDeviceName)).
// Neither `ifconfig wfd0` nor mwud reports that name on the car (probe 2026-11-03: ifconfig has no ssid line,
// 9096 gives no reply), so it is derived from where Connectivity persists it: the CarPlay AP name
// "<name> CarPlay" is that same device name.
inline std::string wfdNoSpaces(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i)
        if (!isspace((unsigned char)s[i])) out.push_back(s[i]);
    return out;
}

inline std::string wfdSsidFromDeviceName(const std::string& name) { return "DIRECT-03-" + wfdNoSpaces(name); }

inline bool wfdDeviceNameFromCarplaySsid(const std::string& carplaySsid, std::string& out) {
    const char* suffix = " CarPlay";
    const size_t sl = strlen(suffix);
    std::string s = wfdTrim(carplaySsid);
    if (s.size() <= sl) return false;
    if (s.compare(s.size() - sl, sl, suffix) != 0) return false;
    std::string name = wfdNoSpaces(wfdTrim(s.substr(0, s.size() - sl)));
    if (name.empty()) return false;
    out = name;
    return true;
}

// Connectivity's WLAN persistency (pers_ConnectivityWlanPersistencyPersMapper) is a small binary blob whose
// printable runs are "<device name>" and "<device name> CarPlay" plus the encrypted passphrase. A J29 read:
//   Toyota51807 | Toyota51807 CarPlay | 240B6170131A7370505E
// Only the run that ends with "CarPlay" is used: it is the device name by construction, and the 20-hex run
// (an encrypted passphrase, see decodeWfdSecret) can never be mistaken for a name.
inline bool wfdDeviceNameFromPersistency(const std::string& text, std::string& out) {
    const char* suffix = "CarPlay";
    const size_t sl = strlen(suffix);
    std::string run;
    for (size_t i = 0; i <= text.size(); ++i) {
        unsigned char c = i < text.size() ? (unsigned char)text[i] : 0;
        if (c >= 0x20 && c < 0x7f) { run.push_back((char)c); continue; }
        if (run.size() > sl && run.compare(run.size() - sl, sl, suffix) == 0) {
            std::string name = wfdNoSpaces(wfdTrim(run.substr(0, run.size() - sl)));
            if (!name.empty()) {
                out = name;
                return true;
            }
        }
        run.clear();
    }
    return false;
}

inline const char* const* wfdPersistPaths() {
    static const char* const p[] = { "/var/opt/sys/persistence/early/pers_ConnectivityWlanPersistencyPersMapper" };
    return p;
}
inline size_t wfdPersistPathCount() { return 1; }

// One request/reply on mwud's command channel (default 127.0.0.1:9096): 8-byte header {u32_le body_len; u32_le
// status} + newline-separated key=value body. No authentication; any local process may drive it (it is the same
// path NetworkingService uses). `info` (optional) reports what happened, for the diagnostic probe: the car gave no
// reply at all on 2026-11-03, so "which half failed" (connect vs read) has to be visible.
// flags: 1 = CRLF line endings in the body, 2 = set the header's second word to 1 (an unknown field some builds
// may use as request type).
inline std::string mwuCommandEx(const std::string& body, int timeout_ms, unsigned flags, std::string* info) {
    std::string note;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        if (info) *info = "socket() failed";
        return std::string();
    }
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(9096);
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(fd, (struct sockaddr*)&sa, sizeof(sa)) != 0) {
        char buf[64];
        snprintf(buf, sizeof(buf), "connect failed (errno %d)", errno);
        if (info) *info = buf;
        close(fd);
        return std::string();
    }
    note = "connected";
    std::string msg;
    unsigned char hdr[8];
    const unsigned len = (unsigned)body.size();
    hdr[0] = (unsigned char)(len & 0xff); hdr[1] = (unsigned char)((len >> 8) & 0xff);
    hdr[2] = (unsigned char)((len >> 16) & 0xff); hdr[3] = (unsigned char)((len >> 24) & 0xff);
    hdr[4] = (unsigned char)((flags & 2) ? 1 : 0);
    hdr[5] = hdr[6] = hdr[7] = 0;
    msg.append((const char*)hdr, 8);
    if (flags & 1) {
        for (size_t i = 0; i < body.size(); ++i) {
            if (body[i] == '\n') msg.push_back('\r');
            msg.push_back(body[i]);
        }
    } else {
        msg += body;
    }
    if (write(fd, msg.data(), msg.size()) != (ssize_t)msg.size()) {
        char buf[96];
        snprintf(buf, sizeof(buf), "connected, write failed (errno %d)", errno);
        if (info) *info = buf;
        close(fd);
        return std::string();
    }
    std::string reply;
    for (;;) {
        struct timeval tv;
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        fd_set rf;
        FD_ZERO(&rf);
        FD_SET(fd, &rf);
        int r = select(fd + 1, &rf, NULL, NULL, &tv);
        if (r <= 0) break;                    // timeout or error: keep what arrived
        char buf[2048];
        ssize_t n = read(fd, buf, sizeof(buf));
        if (n <= 0) break;
        reply.append(buf, (size_t)n);
        if (reply.size() > 8192) break;
    }
    close(fd);
    char buf[96];
    snprintf(buf, sizeof(buf), "%s, %u byte(s) read, %u byte(s) sent", note.c_str(), (unsigned)reply.size(),
             (unsigned)msg.size());
    if (info) *info = buf;
    // strip the reply header
    if (reply.size() >= 8) {
        unsigned bl = 0;
        for (int i = 3; i >= 0; --i) bl = (bl << 8) | (unsigned char)reply[(size_t)i];
        if (bl && reply.size() >= 8 + (size_t)bl) return reply.substr(8, bl);
    }
    return reply.size() > 8 ? reply.substr(8) : std::string();
}

inline std::string mwuCommand(const std::string& body, int timeout_ms, std::string* info = NULL) {
    return mwuCommandEx(body, timeout_ms, 0, info);
}

// SSID ladder, cheapest and most certain first; every rung is logged so one car run shows which one answered.
//  1. NBT_WFD_SSID
//  2. `ifconfig <ifname>` (io-pkt ifconfig can print an ssid line; the car's does not - probe 2026-11-03)
//  3. mwud, p2p_get_cfg then list_groups (no reply on the car - mwud accepts nothing on 9096 there)
//  4. Connectivity's Wi-Fi persistency: "<device name> CarPlay" -> "DIRECT-03-" + name
// The result is cached: a resolved name can only change with a reboot/firmware change.
inline bool resolveWfdSsid(const char* ifname, std::string& ssid) {
    static std::string cached;
    if (!cached.empty()) {
        ssid = cached;
        return true;
    }
    ssid.clear();
    const char* e = getenv("NBT_WFD_SSID");
    if (e && *e) {
        ssid = e;
        printf("[Wireless] Wi-Fi Direct SSID: '%s' (NBT_WFD_SSID)\n", ssid.c_str());
        cached = ssid;
        return true;
    }
    char cmd[96];
    snprintf(cmd, sizeof(cmd), "/sbin/ifconfig %s", ifname ? ifname : "wfd0");
    FILE* p = popen(cmd, "r");
    if (!p) p = popen("ifconfig wfd0", "r");
    if (p) {
        std::string text;
        char line[512];
        while (fgets(line, sizeof(line), p)) text += line;
        pclose(p);
        if (parseIfconfigSsid(text, ssid)) {
            printf("[Wireless] Wi-Fi Direct SSID: '%s' ('%s')\n", ssid.c_str(), cmd);
            cached = ssid;
            return true;
        }
        std::string flat;
        for (size_t i = 0; i < text.size(); ++i) flat.push_back(text[i] == '\n' ? ' ' : text[i]);
        printf("[Wireless] Wi-Fi Direct SSID: '%s' printed no ssid line: '%s'\n", ifname ? ifname : "wfd0",
               wfdTrim(flat).c_str());
    } else {
        printf("[Wireless] Wi-Fi Direct SSID: cannot run '%s'\n", cmd);
    }
    const char* cmds[2] = { "p2p_get_cfg", "list_groups" };
    for (int i = 0; i < 2; ++i) {
        std::string body = "module=wifidirect\niface=";
        body += ifname ? ifname : "wfd0";
        body += "\ncmd=";
        body += cmds[i];
        body += "\n";
        std::string info;
        std::string reply = mwuCommand(body, 1500, &info);
        if (reply.empty()) {
            printf("[Wireless] Wi-Fi Direct SSID: mwud 9096 %s: %s\n", cmds[i], info.c_str());
            continue;
        }
        if (parseMwuSsid(reply, ssid)) {
            printf("[Wireless] Wi-Fi Direct SSID: '%s' (mwud %s)\n", ssid.c_str(), cmds[i]);
            cached = ssid;
            return true;
        }
        std::string flat;
        for (size_t x = 0; x < reply.size() && x < 400; ++x) flat.push_back(reply[x] == '\n' ? ' ' : reply[x]);
        printf("[Wireless] Wi-Fi Direct SSID: mwud %s reply carried no group name: '%s'\n", cmds[i],
               wfdTrim(flat).c_str());
    }
    for (size_t i = 0; i < wfdPersistPathCount(); ++i) {
        std::string text, name;
        if (!readWholeFile(wfdPersistPaths()[i], text)) {
            printf("[Wireless] Wi-Fi Direct SSID: %s not readable\n", wfdPersistPaths()[i]);
            continue;
        }
        if (!wfdDeviceNameFromPersistency(text, name)) continue;
        ssid = wfdSsidFromDeviceName(name);
        printf("[Wireless] Wi-Fi Direct SSID: '%s' (derived: Connectivity persistency names this car '%s', group "
               "= DIRECT-03- + name without spaces)\n", ssid.c_str(), name.c_str());
        cached = ssid;
        return true;
    }
    printf("[Wireless] Wi-Fi Direct SSID: no name from NBT_WFD_SSID, ifconfig, mwud or the WLAN persistency\n");
    return false;
}

} // namespace nbt

#endif // ALT_WIFI_HPP
