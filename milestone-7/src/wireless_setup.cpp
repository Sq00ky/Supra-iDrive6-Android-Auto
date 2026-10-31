// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#include "wireless_setup.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/select.h>

namespace nbt {
namespace wpp {

void putVarint(std::vector<uint8_t>& out, uint64_t v) {
    while (v >= 0x80) { out.push_back((uint8_t)(v | 0x80)); v >>= 7; }
    out.push_back((uint8_t)v);
}

void putString(std::vector<uint8_t>& out, int field, const std::string& s) {
    putVarint(out, ((uint64_t)field << 3) | 2);
    putVarint(out, s.size());
    out.insert(out.end(), s.begin(), s.end());
}

void putInt(std::vector<uint8_t>& out, int field, uint64_t v) {
    putVarint(out, ((uint64_t)field << 3) | 0);
    putVarint(out, v);
}

bool getVarint(const uint8_t* p, size_t n, size_t& pos, uint64_t& v) {
    v = 0;
    int shift = 0;
    while (pos < n && shift < 64) {
        uint8_t b = p[pos++];
        v |= (uint64_t)(b & 0x7f) << shift;
        if (!(b & 0x80)) return true;
        shift += 7;
    }
    return false;
}

bool findVarintField(const uint8_t* p, size_t n, int field, uint64_t& v) {
    size_t pos = 0;
    while (pos < n) {
        uint64_t key;
        if (!getVarint(p, n, pos, key)) return false;
        int f = (int)(key >> 3);
        int wt = (int)(key & 7);
        if (wt == 0) {
            uint64_t x;
            if (!getVarint(p, n, pos, x)) return false;
            if (f == field) { v = x; return true; }
        } else if (wt == 2) {
            uint64_t len;
            if (!getVarint(p, n, pos, len)) return false;
            if (pos + len > n) return false;
            pos += (size_t)len;
        } else if (wt == 5) {
            pos += 4;
        } else if (wt == 1) {
            pos += 8;
        } else {
            return false;
        }
    }
    return false;
}

std::vector<uint8_t> frame(uint16_t id, const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> f;
    f.push_back((uint8_t)(payload.size() >> 8));
    f.push_back((uint8_t)(payload.size() & 0xff));
    f.push_back((uint8_t)(id >> 8));
    f.push_back((uint8_t)(id & 0xff));
    f.insert(f.end(), payload.begin(), payload.end());
    return f;
}

std::vector<uint8_t> buildStartRequest(const std::string& ip, uint16_t port) {
    std::vector<uint8_t> p;
    putString(p, 1, ip);
    putInt(p, 2, port);
    return frame(kWifiStartRequest, p);
}

std::vector<uint8_t> buildInfoResponse(const ApInfo& ap) {
    std::vector<uint8_t> p;
    putString(p, 1, ap.ssid);
    putString(p, 2, ap.key);
    putString(p, 3, ap.bssid);
    putInt(p, 4, (uint64_t)ap.security);
    putInt(p, 5, (uint64_t)ap.apType);
    return frame(kWifiInfoResponse, p);
}

} // namespace wpp

static std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && isspace((unsigned char)s[a])) ++a;
    while (b > a && isspace((unsigned char)s[b - 1])) --b;
    return s.substr(a, b - a);
}

bool loadCarPlayApConfig(const char* path, ApInfo& out, bool quiet) {
    FILE* f = fopen(path, "r");
    if (!f) {
        if (!quiet) printf("[Wireless] Cannot open %s: %s\n", path, strerror(errno));
        return false;
    }
    char line[512];
    bool inWifi = false;
    bool haveSsid = false, haveKey = false;
    while (fgets(line, sizeof(line), f)) {
        std::string s = trim(line);
        if (s.empty() || s[0] == ';' || s[0] == '#') continue;
        if (s[0] == '[') {
            inWifi = (s == "[WiFi]" || s == "[Wifi]" || s == "[wifi]");
            continue;
        }
        if (!inWifi) continue;
        size_t eq = s.find('=');
        if (eq == std::string::npos) continue;
        std::string k = trim(s.substr(0, eq));
        std::string v = trim(s.substr(eq + 1));
        if (k == "SSID")            { out.ssid = v; haveSsid = true; }
        else if (k == "Passphrase") { out.key = v;  haveKey = true; }
        else if (k == "BSSID" && v.size() == 12) {
            std::string m;
            for (size_t i = 0; i < 12; i += 2) {
                if (i) m += ':';
                m += (char)toupper((unsigned char)v[i]);
                m += (char)toupper((unsigned char)v[i + 1]);
            }
            out.bssid = m;
        }
        else if (k == "SecurityType") {
            // CarPlay cfg: WPA2 / WPA2 Personal / 2 -> WPA2_PERSONAL (8); None -> open (0)
            if (v == "None" || v == "0") out.security = 0;
            else out.security = 8;
        }
    }
    fclose(f);
    if (!quiet)
        printf("[Wireless] CarPlay AP config: ssid='%s' passphrase=%s bssid='%s' security=%d\n",
               out.ssid.c_str(), haveKey ? "(read)" : "(missing)", out.bssid.c_str(), out.security);
    return haveSsid && haveKey;
}

// Connectivity (CDipoWlanCarPlayClient::onApConfigurationUpdate -> FUN_00608d28 -> CDipoFileHandler FUN_006130c8)
// rewrites iap2.cfg whenever the AP's SSID, passphrase, channel, name or version differ from the file, but rewrites
// iap2_Wifi.cfg only when name / channel / version / A4A change: it never compares SSID or passphrase there, so after
// the car generates a new AP password iap2_Wifi.cfg keeps the old one. iap2.cfg is also the config of the Bluetooth
// iAP2 driver (/dev/ipod_bt), the link that hands the Wi-Fi details to an iPhone. So iap2.cfg is the source of truth.
const char* const kApCfgPaths[] = {
    "/var/opt/conn/carplay/iap2.cfg",
    "/fs/sda1/opt/conn/carplay/iap2.cfg",
    "/var/opt/conn/carplay/iap2_Wifi.cfg",        // fallback only: SSID / passphrase can be stale
    "/fs/sda1/opt/conn/carplay/iap2_Wifi.cfg",
    // Not /fs/sda0/opt/conn/data/carplay/*: factory templates (stock SSID, placeholder passphrase) on every car seen.
};
const size_t kApCfgPathCount = sizeof(kApCfgPaths) / sizeof(kApCfgPaths[0]);

static void addCandidate(std::vector<ApCandidate>& out, const ApInfo& ap, const std::string& path) {
    for (size_t i = 0; i < out.size(); ++i)
        if (out[i].ap.ssid == ap.ssid && out[i].ap.key == ap.key) return;
    ApCandidate c;
    c.ap = ap;
    c.path = path;
    out.push_back(c);
}

std::vector<ApCandidate> collectApCandidates(const char* const* paths, size_t nPaths) {
    std::vector<ApCandidate> out;
    std::vector<std::string> order;
    for (size_t i = 0; i < nPaths; ++i) {
        bool dup = false;
        for (size_t j = 0; j < order.size(); ++j) if (order[j] == paths[i]) dup = true;
        if (!dup) order.push_back(paths[i]);
    }
    std::string liveSsid;
    for (size_t i = 0; i < order.size(); ++i) {
        ApInfo ap;
        if (!loadCarPlayApConfig(order[i].c_str(), ap, true) || ap.key.empty()) continue;
        if (liveSsid.empty()) liveSsid = ap.ssid;
        if (ap.ssid != liveSsid) {        // template copy with a stock SSID: try its key on the live SSID too
            ApInfo live = ap;
            live.ssid = liveSsid;
            addCandidate(out, live, order[i]);
        }
        addCandidate(out, ap, order[i]);
    }
    return out;
}

void fillApNetworkInfo(const char* ifname, ApInfo& ap) {
    const char* eIp = getenv("NBT_AP_IP");
    const char* eMac = getenv("NBT_AP_BSSID");
    if (eIp && *eIp) ap.ip = eIp;
    if (eMac && *eMac) ap.bssid = eMac;
    if (!ap.ip.empty() && !ap.bssid.empty()) return;

    char cmd[96];
    snprintf(cmd, sizeof(cmd), "/sbin/ifconfig %s", ifname);
    FILE* p = popen(cmd, "r");
    if (!p) p = popen("ifconfig uap0", "r");
    if (!p) {
        printf("[Wireless] popen(ifconfig) failed: %s\n", strerror(errno));
        return;
    }
    char line[256];
    while (fgets(line, sizeof(line), p)) {
        std::string s = trim(line);
        if (ap.ip.empty() && s.compare(0, 5, "inet ") == 0) {
            size_t a = 5;
            size_t b = s.find(' ', a);
            ap.ip = s.substr(a, b == std::string::npos ? std::string::npos : b - a);
        }
        if (ap.bssid.empty() && (s.compare(0, 8, "address:") == 0 || s.compare(0, 6, "ether ") == 0)) {
            size_t a = s.find(':');
            if (s[0] == 'e') a = 5;
            std::string m = trim(s.substr(a + 1));
            if (m.size() >= 17) {
                m = m.substr(0, 17);
                for (size_t i = 0; i < m.size(); ++i) m[i] = (char)toupper((unsigned char)m[i]);
                ap.bssid = m;
            }
        }
    }
    pclose(p);
    printf("[Wireless] %s: ip='%s' bssid='%s'\n", ifname, ap.ip.c_str(), ap.bssid.c_str());
}

static bool writeAll(int fd, const std::vector<uint8_t>& d) {
    size_t off = 0;
    {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        printf("[Wireless] RAW TX t=%ld +%u\n", (long)(tv.tv_sec % 100000) * 1000 + tv.tv_usec / 1000, (unsigned)d.size());
    }
    while (off < d.size()) {
        ssize_t n = write(fd, &d[off], d.size() - off);
        if (n < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN) { usleep(10000); continue; }
            return false;
        }
        off += (size_t)n;
    }
    return true;
}

static int g_lastReadEnd = 0;   // 0 = ok, 1 = timeout, 2 = closed by peer, 3 = error

static long nowMs() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long)(tv.tv_sec % 100000) * 1000 + tv.tv_usec / 1000;
}

// ---- buffered RFCOMM reader --------------------------------------------------------------------
// The head unit's btspp device behaves message-oriented: a read() with a buffer smaller than the
// pending frame throws the rest away (the old 4-byte-header / N-byte-payload reads lost data and
// turned the next frame into garbage). So: always read with a large buffer and frame ourselves.
static uint8_t g_rx[16384];
static size_t g_rxLen = 0;

static bool g_foreign = false;   // the link turned out to be BMW Connected (BCL/Etch), not Android Auto

static bool g_spokeWpp = false;  // a valid Android Auto wireless frame came back on this link
static int g_peekMs = 0;         // setSppPeekMs()

void setSppPeekMs(int ms) { g_peekMs = ms < 0 ? 0 : (ms > 3000 ? 3000 : ms); }

static void rxReset() { g_rxLen = 0; g_lastReadEnd = 0; g_foreign = false; g_spokeWpp = false; }

// BMW Connected / MyBMW talks BCL over its own SPP link: an 8-byte header (u16 command, u16 src port,
// u16 dst port, u16 length) carrying Apache Etch packets that start with the 0xDEADBEEF signature
// (seen on a friend's BMW: 00 02 00 11 0f a4 00 3c de ad be ef ... = DATA to port 4004). If that link
// gets btspp0, it is not ours; recognise it so the caller can release it and look at the next node.
// A second capture had only the BCL watchdog: 00 02 13 89 13 89 00 04 13 37 13 37 (DATA, port 5001 ->
// 5001, 4 bytes 0x1337 0x1337). So also accept any self-consistent BCL frame (u16 length at offset 6
// equal to the bytes that follow the 8-byte header) that is not a valid Android Auto wireless frame
// ([u16 len = n - 4][u16 id 1..32]).
// The BCL link also opens with a bare 4-byte startup magic 12 34 56 78 (BimmerGestalt wireshark_bmw_bcl:
// "BMW BCL Startup"); a BMW (2026-10-30, nbt-aad log umass02200t7) sent only that, twice, on 53 links that
// never answered our WifiStartRequest, so the daemon kept feeding the Wi-Fi details to the BMW app.
bool looksLikeBmwBcl(const uint8_t* p, size_t n) {
    if (n >= 4 && p[0] == 0x12 && p[1] == 0x34 && p[2] == 0x56 && p[3] == 0x78) return true;
    for (size_t i = 0; i + 4 <= n; ++i) {
        if (p[i] == 0xde && p[i + 1] == 0xad && p[i + 2] == 0xbe && p[i + 3] == 0xef) return true;
        if (p[i] == 0x13 && p[i + 1] == 0x37 && i + 4 <= n && p[i + 2] == 0x13 && p[i + 3] == 0x37) return true;
    }
    if (n < 8) return false;
    unsigned aaLen = ((unsigned)p[0] << 8) | p[1], aaId = ((unsigned)p[2] << 8) | p[3];
    if (aaLen == n - 4 && aaId >= 1 && aaId <= 32) return false;
    unsigned cmd = aaLen, bclLen = ((unsigned)p[6] << 8) | p[7];
    return cmd >= 1 && cmd <= 0x20 && bclLen == n - 8;
}

bool lastLinkForeign() { return g_foreign; }
bool lastLinkSpokeWpp() { return g_spokeWpp; }

// 1 = bytes added, 0 = timeout / nothing, -1 = link closed or broken
static int rxFill(int fd, int timeout_ms) {
    fd_set rf;
    FD_ZERO(&rf);
    FD_SET(fd, &rf);
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    int r = select(fd + 1, &rf, NULL, NULL, &tv);
    if (r == 0) { g_lastReadEnd = 1; return 0; }
    if (r < 0) { if (errno == EINTR) return 0; g_lastReadEnd = 3; return -1; }
    uint8_t tmp[2048];
    ssize_t k = read(fd, tmp, sizeof(tmp));
    if (k == 0) { g_lastReadEnd = 2; return -1; }
    if (k < 0) { if (errno == EINTR || errno == EAGAIN) return 0; g_lastReadEnd = 3; return -1; }
    printf("[Wireless] RAW RX t=%ld +%d:", nowMs(), (int)k);
    for (ssize_t i = 0; i < k && i < 48; ++i) printf(" %02x", tmp[i]);
    printf("\n");
    if (looksLikeBmwBcl(tmp, (size_t)k)) {
        printf("[Wireless] this Bluetooth serial link is the BMW/Toyota companion app (BCL), not Android Auto\n");
        g_foreign = true;
        g_lastReadEnd = 4;
        return -1;
    }
    size_t room = sizeof(g_rx) - g_rxLen;
    size_t take = (size_t)k < room ? (size_t)k : room;
    memcpy(g_rx + g_rxLen, tmp, take);
    g_rxLen += take;
    return 1;
}

// Extracts one complete [len][id][payload] message from the buffer.
static bool rxNext(uint16_t& id, std::vector<uint8_t>& pl) {
    while (g_rxLen >= 4) {
        uint16_t len = (uint16_t)((g_rx[0] << 8) | g_rx[1]);
        id = (uint16_t)((g_rx[2] << 8) | g_rx[3]);
        if (len > 4096 || id == 0 || id > 32) {
            printf("[Wireless] out of sync (hdr %02x %02x %02x %02x): dropping 1 byte\n", g_rx[0], g_rx[1], g_rx[2], g_rx[3]);
            memmove(g_rx, g_rx + 1, --g_rxLen);
            continue;
        }
        if (g_rxLen < (size_t)4 + len) return false;
        pl.assign(g_rx + 4, g_rx + 4 + len);
        g_spokeWpp = true;
        g_rxLen -= (size_t)4 + len;
        memmove(g_rx, g_rx + 4 + len, g_rxLen);
        return true;
    }
    return false;
}

static void hexdump(const char* tag, const uint8_t* p, size_t n) {
    printf("[Wireless] %s (%u bytes):", tag, (unsigned)n);
    for (size_t i = 0; i < n && i < 64; ++i) printf(" %02x", p[i]);
    printf("\n");
}

// Generic protobuf field dump (no schema): shows what the phone really sent.
static void dumpProto(const uint8_t* p, size_t n) {
    size_t pos = 0;
    printf("[Wireless]    fields:");
    while (pos < n) {
        uint64_t key;
        if (!wpp::getVarint(p, n, pos, key)) { printf(" <bad key>"); break; }
        int f = (int)(key >> 3), wt = (int)(key & 7);
        if (f == 0) { printf(" <field 0: not protobuf>"); break; }
        if (wt == 0) {
            uint64_t v;
            if (!wpp::getVarint(p, n, pos, v)) { printf(" <bad varint>"); break; }
            printf(" f%d=%llu", f, (unsigned long long)v);
        } else if (wt == 2) {
            uint64_t len;
            if (!wpp::getVarint(p, n, pos, len) || pos + len > n) { printf(" <bad len>"); break; }
            bool pr = true;
            for (size_t i = 0; i < len; ++i) if (p[pos + i] < 32 || p[pos + i] > 126) pr = false;
            if (pr) printf(" f%d='%.*s'", f, (int)len, (const char*)(p + pos));
            else printf(" f%d=<%u bytes>", f, (unsigned)len);
            pos += (size_t)len;
        } else if (wt == 5) { printf(" f%d=<fixed32>", f); pos += 4;
        } else if (wt == 1) { printf(" f%d=<fixed64>", f); pos += 8;
        } else { printf(" <wiretype %d>", wt); break; }
    }
    printf("\n");
}

// Handles one phone message. Returns 0 = keep going, 1 = handshake finished OK, -1 = write failure.
static int handleMsg(int fd, uint16_t id, const std::vector<uint8_t>& pl, const ApInfo& ap, uint16_t port,
                     bool& sentInfo, int& startRespSeen) {
    printf("[Wireless] RX msg id=%u len=%u\n", id, (unsigned)pl.size());
    if (!pl.empty()) { hexdump("   payload", &pl[0], pl.size()); dumpProto(&pl[0], pl.size()); }
    if (id == wpp::kWifiInfoRequest) {
        std::vector<uint8_t> resp = wpp::buildInfoResponse(ap);
        printf("[Wireless] TX WifiInfoResponse ssid='%s' bssid='%s' security=%d\n",
               ap.ssid.c_str(), ap.bssid.c_str(), ap.security);
        if (!writeAll(fd, resp)) return -1;
        sentInfo = true;
    } else if (id == wpp::kWifiStartResponse) {
        uint64_t st = 0;
        if (!pl.empty() && wpp::findVarintField(&pl[0], pl.size(), 3, st))
            printf("[Wireless] WifiStartResponse status field(3)=%llu\n", (unsigned long long)st);
        else
            printf("[Wireless] WifiStartResponse (no status field)\n");
        if (!sentInfo) ++startRespSeen;
    } else if (id == wpp::kWifiConnectStatus) {
        uint64_t st = 0;
        bool have = !pl.empty() && (wpp::findVarintField(&pl[0], pl.size(), 1, st) || wpp::findVarintField(&pl[0], pl.size(), 2, st));
        printf("[Wireless] WifiConnectStatus status=%llu\n", (unsigned long long)(have ? st : 0));
        return (have ? (st == 0 || st == 12) : true) ? 1 : 0;
    } else if (id == wpp::kWifiStartRequest) {
        // The phone can also start the exchange itself ("MD initiated"): confirm with ip/port/SUCCESS.
        std::vector<uint8_t> p;
        wpp::putString(p, 1, ap.ip);
        wpp::putInt(p, 2, port);
        wpp::putInt(p, 3, 0);   // status 0 = SUCCESS (the phone itself sends `18 00`)
        std::vector<uint8_t> rsp = wpp::frame(wpp::kWifiStartResponse, p);
        printf("[Wireless] phone sent its own WifiStartRequest: answering WifiStartResponse ip=%s port=%u status=0\n",
               ap.ip.c_str(), (unsigned)port);
        if (!writeAll(fd, rsp)) return -1;
    } else if (id == 8) {
        std::vector<uint8_t> pong = wpp::frame(9, pl);   // WifiPingRequest -> WifiPingResponse (echo)
        printf("[Wireless] ping -> pong\n");
        if (!writeAll(fd, pong)) return -1;
    } else if (id == wpp::kWifiVersionResponse) {
        printf("[Wireless] WifiVersionResponse from phone (the phone speaks the newer WPP)\n");
    } else {
        printf("[Wireless] ignoring unknown msg id=%u\n", id);
    }
    return 0;
}

bool runWirelessHandshake(int fd, const ApInfo& ap, uint16_t port, int timeout_ms, int mode) {
    static const char* const kModeName[3] = { "legacy", "version-first", "proactive-info" };
    if (mode < 0 || mode > 2) mode = 0;
    printf("[Wireless] handshake mode %d (%s)\n", mode, kModeName[mode]);
    rxReset();
    // Listen before talking: if the peer is MyBMW / BMW Connected (the link AAIdrive uses) and has already sent its
    // BCL startup magic, write nothing into it. Android Auto waits up to 10 s for our first message.
    if (g_peekMs > 0) {
        int f = rxFill(fd, g_peekMs);
        if (f < 0) {
            if (g_foreign)
                printf("[Wireless] the BMW app spoke first on this link: nothing written, leaving it to the car\n");
            else
                printf("[Wireless] link closed before we wrote anything\n");
            return false;
        }
    }
    std::vector<uint8_t> req = wpp::buildStartRequest(ap.ip, port);
    std::vector<uint8_t> info = wpp::buildInfoResponse(ap);
    if (mode == 1) {
        std::vector<uint8_t> ver = wpp::frame(wpp::kWifiVersionRequest, std::vector<uint8_t>());
        hexdump("TX WifiVersionRequest", &ver[0], ver.size());
        if (!writeAll(fd, ver)) {
            printf("[Wireless] write WifiVersionRequest failed: %s\n", strerror(errno));
            return false;
        }
    }
    hexdump("TX WifiStartRequest", &req[0], req.size());
    if (!writeAll(fd, req)) {
        printf("[Wireless] write WifiStartRequest failed: %s\n", strerror(errno));
        return false;
    }
    bool sentInfo = false;
    if (mode == 2) {
        printf("[Wireless] TX WifiInfoResponse (proactive) ssid='%s' bssid='%s' security=%d\n",
               ap.ssid.c_str(), ap.bssid.c_str(), ap.security);
        if (!writeAll(fd, info)) return false;
        sentInfo = true;
    }
    int startRespSeen = 0;
    long tStart = nowMs();
    long tInfo = sentInfo ? tStart : 0;
    int resends = 0;
    for (;;) {
        uint16_t id; std::vector<uint8_t> pl;
        while (rxNext(id, pl)) {
            int r = handleMsg(fd, id, pl, ap, port, sentInfo, startRespSeen);
            if (r < 0) return false;
            if (sentInfo && tInfo == 0) tInfo = nowMs();
            if (r > 0) return true;
        }
        int f = rxFill(fd, 1000);
        if (f < 0 && g_foreign) return false;
        if (f < 0) {
            if (sentInfo) {
                printf("[Wireless] phone closed the Bluetooth link after our WifiInfoResponse: it should now join the Wi-Fi and connect over TCP\n");
                return true;
            }
            printf("[Wireless] phone closed the Bluetooth link before the AP info was exchanged\n");
            return false;
        }
        if (f == 0) {
            long now = nowMs();
            if (sentInfo) {
                // Give the phone time to join the AP; the TCP connect to our port is the real success signal.
                if (now - tInfo > 45000) {
                    printf("[Wireless] phone sent no status after WifiInfoResponse; assuming it is joining Wi-Fi\n");
                    return true;
                }
            } else if (now - tStart > timeout_ms) {
                if (startRespSeen && mode == 0 && resends < 2) {
                    ++resends;
                    printf("[Wireless] no WifiInfoRequest after msg 7; re-sending WifiStartRequest (%d/2)\n", resends);
                    hexdump("TX WifiStartRequest", &req[0], req.size());
                    if (!writeAll(fd, req)) return false;
                    tStart = now;
                    continue;
                }
                printf("[Wireless] timeout waiting for the phone\n");
                return false;
            }
        }
    }
}

bool wppService(int fd, int timeout_ms, const ApInfo& ap, uint16_t port) {
    int f = rxFill(fd, timeout_ms);
    uint16_t id; std::vector<uint8_t> pl;
    bool sentInfo = true; int sr = 0;
    while (rxNext(id, pl)) {
        if (handleMsg(fd, id, pl, ap, port, sentInfo, sr) < 0) return false;
    }
    return f >= 0;
}

} // namespace nbt
