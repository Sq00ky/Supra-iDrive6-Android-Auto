// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#include "wireless_setup.hpp"
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
using namespace nbt;

static ApInfo g_ap;
static int g_fd;
static void* phone(void*) {
    // read WifiStartRequest
    uint8_t h[4]; read(g_fd, h, 4);
    int len = (h[0] << 8) | h[1]; int id = (h[2] << 8) | h[3];
    uint8_t b[256]; read(g_fd, b, len);
    assert(id == 1);
    // expect ip string tag 0x0a, port field 2
    assert(b[0] == 0x0a && b[1] == (uint8_t)g_ap.ip.size());
    uint64_t port = 0; assert(wpp::findVarintField(b, len, 2, port) && port == 52770);
    // send WifiStartResponse (7) status=0, then WifiInfoRequest (2)
    std::vector<uint8_t> p; wpp::putString(p, 2, "172.16.224.1"); wpp::putInt(p, 3, 52770); wpp::putInt(p, 4, 0);
    std::vector<uint8_t> f = wpp::frame(7, p); write(g_fd, &f[0], f.size());
    std::vector<uint8_t> e; f = wpp::frame(2, e); write(g_fd, &f[0], f.size());
    read(g_fd, h, 4); len = (h[0] << 8) | h[1]; id = (h[2] << 8) | h[3];
    read(g_fd, b, len);
    assert(id == 3);
    assert(b[0] == 0x0a && memcmp(b + 2, "Toyota51807 CarPlay", 19) == 0);
    uint64_t sec = 0, apt = 0;
    assert(wpp::findVarintField(b, len, 4, sec) && sec == 8);
    assert(wpp::findVarintField(b, len, 5, apt) && apt == 1);
    std::vector<uint8_t> s; wpp::putInt(s, 1, 0);
    f = wpp::frame(6, s); write(g_fd, &f[0], f.size());
    return NULL;
}

int main() {
    // cfg parser
    FILE* c = fopen("t_iap2_wifi.cfg", "w");
    fputs("[accessory]\nname=x\n[WiFi]\nenable=yes\n;BSSID=48a9d2ba070b\nSSID=Toyota51807 CarPlay\nPassphrase=testpass123\nSecurityType=WPA2\nChannel=1\n[other]\nSSID=no\n", c);
    fclose(c);
    ApInfo ap; assert(loadCarPlayApConfig("t_iap2_wifi.cfg", ap));
    assert(ap.ssid == "Toyota51807 CarPlay" && ap.key == "testpass123" && ap.security == 8);
    ap.ip = "172.16.224.1"; ap.bssid = "48:A9:D2:BA:07:0B"; g_ap = ap;
    // wire sanity vs. documented example: field 3 bssid must use tag 0x1a
    std::vector<uint8_t> r = wpp::buildInfoResponse(ap);
    bool found = false; for (size_t i = 4; i + 1 < r.size(); ++i) if (r[i] == 0x1a && r[i+1] == 17) found = true;
    assert(found);
    int sv[2]; assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    g_fd = sv[1];
    pthread_t t; pthread_create(&t, NULL, phone, NULL);
    bool ok = runWirelessHandshake(sv[0], ap, 52770, 2000);
    pthread_join(t, NULL);
    assert(ok);
    printf("test_wpp OK\n");
    return 0;
}
