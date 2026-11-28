// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// vinprobe: can this head unit's VIN be read without the car's diagnostics? Read-only: without -z it sends nothing.
// Tries every source the derived Wi-Fi Direct name uses (alt_wifi.hpp wfdVinTail) plus a few more, prints what each
// one gave, and the name nbt-aad would derive for each brand.
//   usage: vinprobe [-q] [-z]   -q = mask the VIN's middle characters (for posting the output publicly)
//                               -z = also ask the car's gateway (ZGW): one DoIP vehicle identification request
//                                    (UDP 13400, payload 0x0001) and one HSFZ identification (UDP 6811, 00000000 0011)
//                                    per interface broadcast; prints the replies. Identification only, no diagnostics.
#include "../src/alt_wifi.hpp"
#include "../src/wfd_own.hpp"

#include <stdio.h>
#include <vector>
#include <string.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <unistd.h>
#include <errno.h>

using namespace nbt;

static bool g_mask = false;

static std::string show(const std::string& v) {
    if (!g_mask || v.size() <= 5) return v;
    std::string s(v.size() - 5, '*');
    return s + v.substr(v.size() - 5);
}

static void tryFile(const char* path, int kind) {   // kind 0 = SWT records, 1 = raw last 7, 2 = full VIN in a blob
    struct stat st;
    if (stat(path, &st) != 0) { printf("  %-80s missing\n", path); return; }
    std::string text;
    if (!readWholeFile(path, text)) { printf("  %-80s exists (%ld bytes) but not readable\n", path, (long)st.st_size); return; }
    std::string got;
    bool ok = false;
    if (kind == 0) ok = wfdVinTailFromSwt(text, got);
    else if (kind == 1) {
        if (text.size() >= 7) {
            got = text.substr(0, 7);
            ok = true;
            for (size_t i = 0; i < got.size() && ok; ++i) ok = wfdVinChar(got[i]);
        }
    } else ok = wfdVinFromBlob(text, got);
    printf("  %-80s %ld bytes: %s\n", path, (long)text.size(),
           ok ? (std::string(kind == 2 ? "VIN " : "VIN tail ") + show(got)).c_str() : "no VIN found");
}

static void hexdump(const unsigned char* b, int n) {
    printf("      ");
    for (int i = 0; i < n && i < 48; ++i) printf("%02x", b[i]);
    printf("%s\n", n > 48 ? "..." : "");
}

// A VIN in a reply: DoIP announcement (type 0x0004: VIN at offset 8) or HSFZ text ("BMWVIN" + 17).
static bool vinFromReply(const unsigned char* b, int n, std::string& vin, std::string& what) {
    if (n >= 8 + 17 && b[1] == (unsigned char)~b[0] && b[2] == 0x00 && b[3] == 0x04) {
        vin.assign((const char*)b + 8, 17);
        char extra[96];
        snprintf(extra, sizeof extra, "DoIP announcement, logical address 0x%02x%02x, EID %02x:%02x:%02x:%02x:%02x:%02x",
                 n > 26 ? b[25] : 0, n > 26 ? b[26] : 0, n > 32 ? b[27] : 0, n > 32 ? b[28] : 0, n > 32 ? b[29] : 0,
                 n > 32 ? b[30] : 0, n > 32 ? b[31] : 0, n > 32 ? b[32] : 0);
        what = extra;
        return true;
    }
    const std::string t((const char*)b, n);
    const size_t k = t.find("BMWVIN");
    if (k != std::string::npos && k + 6 + 17 <= t.size()) {
        vin = t.substr(k + 6, 17);
        const size_t d = t.find("DIAGADR");
        what = "HSFZ identification" + (d != std::string::npos ? std::string(", ") + t.substr(d, 9) : std::string());
        return true;
    }
    return wfdVinFromBlob(t, vin) && (what = "VIN-shaped text in the reply", true);
}

static int zgwProbe() {
    printf("gateway (ZGW) identification, -z:\n");
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) { printf("  socket: %s\n", strerror(errno)); return 0; }
    int on = 1;
    setsockopt(s, SOL_SOCKET, SO_BROADCAST, &on, sizeof on);
    sockaddr_in me;
    memset(&me, 0, sizeof me);
    me.sin_family = AF_INET;
    me.sin_len = sizeof me;
    bind(s, (sockaddr*)&me, sizeof me);
    static const unsigned char kDoip[2][8] = { { 0x02, 0xFD, 0x00, 0x01, 0, 0, 0, 0 },   // ISO 13400-2:2012
                                                { 0xFF, 0x00, 0x00, 0x01, 0, 0, 0, 0 } };  // "any version"
    static const unsigned char kHsfz[6] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x11 };
    std::vector<in_addr> dst;
    ifaddrs* ifs = 0;
    if (getifaddrs(&ifs) == 0) {
        for (ifaddrs* i = ifs; i; i = i->ifa_next) {
            if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET || (i->ifa_flags & IFF_LOOPBACK) ||
                !(i->ifa_flags & IFF_UP) || !(i->ifa_flags & IFF_BROADCAST) || !i->ifa_broadaddr)
                continue;
            const in_addr b = ((sockaddr_in*)i->ifa_broadaddr)->sin_addr;
            char a[32], bb[32];
            snprintf(a, sizeof a, "%s", inet_ntoa(((sockaddr_in*)i->ifa_addr)->sin_addr));
            snprintf(bb, sizeof bb, "%s", inet_ntoa(b));
            printf("  %-10s %-16s -> broadcast %s\n", i->ifa_name, a, bb);
            dst.push_back(b);
        }
        freeifaddrs(ifs);
    }
    in_addr all;
    all.s_addr = htonl(INADDR_BROADCAST);
    dst.push_back(all);
    for (size_t k = 0; k < dst.size(); ++k) {
        sockaddr_in to;
        memset(&to, 0, sizeof to);
        to.sin_family = AF_INET;
        to.sin_len = sizeof to;
        to.sin_addr = dst[k];
        to.sin_port = htons(13400);
        for (int v = 0; v < 2; ++v) sendto(s, kDoip[v], sizeof kDoip[v], 0, (sockaddr*)&to, sizeof to);
        to.sin_port = htons(6811);
        sendto(s, kHsfz, sizeof kHsfz, 0, (sockaddr*)&to, sizeof to);
    }
    printf("  sent DoIP 13400 (x2) + HSFZ 6811 to %u address(es); listening 3 s\n", (unsigned)dst.size());
    int found = 0, replies = 0;
    timeval end, now;
    gettimeofday(&end, 0);
    end.tv_sec += 3;
    for (;;) {
        gettimeofday(&now, 0);
        long ms = (end.tv_sec - now.tv_sec) * 1000 + (end.tv_usec - now.tv_usec) / 1000;
        if (ms <= 0) break;
        fd_set rf;
        FD_ZERO(&rf);
        FD_SET(s, &rf);
        timeval tv = { ms / 1000, (ms % 1000) * 1000 };
        if (select(s + 1, &rf, 0, 0, &tv) <= 0) break;
        unsigned char buf[1500];
        sockaddr_in from;
        socklen_t fl = sizeof from;
        const int n = recvfrom(s, buf, sizeof buf, 0, (sockaddr*)&from, &fl);
        if (n <= 0) continue;
        ++replies;
        std::string vin, what;
        const bool ok = vinFromReply(buf, n, vin, what);
        printf("  reply from %s:%u, %d bytes: %s\n", inet_ntoa(from.sin_addr), ntohs(from.sin_port), n,
               ok ? ("VIN " + show(vin) + " (" + what + ")").c_str() : "no VIN in it");
        hexdump(buf, ok && g_mask ? 0 : n);
        if (ok) ++found;
    }
    close(s);
    if (!replies) printf("  no reply from anything (the gateway does not answer the head unit, or not on these ports)\n");
    return found;
}

int main(int argc, char** argv) {
    bool zgw = false;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "-q")) g_mask = true;
        if (!strcmp(argv[i], "-z")) zgw = true;
    }
    printf("vinprobe: where can the VIN be read on this head unit? (read-only)\n");
    printf("software-update records (last 7 per record):\n");
    for (size_t i = 0; i < wfdSwtPathCount(); ++i) tryFile(wfdSwtPaths()[i], 0);
    tryFile("/net/hu-omap/var/opt/sys/persistence/early/pers_NBTCarHUSwt", 0);
    printf("navigation (last 7, raw):\n");
    tryFile("/var/opt/nav/vin.bin", 1);
    printf("full VIN in persistency blobs:\n");
    tryFile("/net/hu-jacinto/var/opt/sys/persistence/early/pers_NbtEvoJacintoDiagHUHighAppCodingController", 2);
    tryFile("/var/opt/sys/persistence/early/pers_NbtEvoJacintoDiagHUHighAppCodingController", 2);
    tryFile("/var/opt/sys/persistence/early/pers_OnlineRDC", 2);
    tryFile("/dev/shmem/sysetpage", 2);
    printf("Wi-Fi Direct name today (head-unit ladder):\n");
    std::string ssid;
    if (resolveWfdSsid("wfd0", ssid)) printf("  resolved: '%s'\n", ssid.c_str());   // a group name holds only the last 5, shown with -q anyway
    else printf("  not resolved from the head unit's files\n");
    if (zgw) zgwProbe();
    std::string tail, from;
    if (!wfdVinTail(tail, from)) {
        printf("RESULT: no VIN from any source nbt-aad uses\n");
        return 1;
    }
    printf("RESULT: VIN tail %s from %s\n", show(tail).c_str(), from.c_str());
    static const char* const kBrands[] = { "BMW", "MINI", "Toyota", "Rolls-Royce" };
    for (size_t i = 0; i < 4; ++i)
        printf("  derived name for %-11s %s%s\n", kBrands[i], wfdSsidFromBrandVin(kBrands[i], tail).c_str(),
               !ssid.empty() && wfdSsidFromBrandVin(kBrands[i], tail) == ssid ? "   <- matches the car's name" : "");
    return 0;
}
