// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Host test: collectApCandidates() with a stale iap2_Wifi.cfg next to the in-sync iap2.cfg.
#include "wireless_setup.hpp"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

static void put(const char* p, const char* ssid, const char* key) {
    FILE* f = fopen(p, "w");
    fprintf(f, "[accessory]\nname=x\n[WiFi]\nenable=yes\n;BSSID=48a9d2ba070b\nSSID=%s\nPassphrase=%s\nSecurityType=WPA2\nChannel=1\n", ssid, key);
    fclose(f);
}

int main() {
    const char* missing = "/tmp/nbt_ap_missing.cfg";
    const char* iap2 = "/tmp/nbt_ap_iap2.cfg";        // in sync with the AP
    const char* wifi = "/tmp/nbt_ap_wifi.cfg";        // stale passphrase
    unlink(missing);
    put(iap2, "BMW51709 CarPlay", "currentKey");
    put(wifi, "BMW51709 CarPlay", "staleKey01");
    const char* paths[] = { missing, iap2, wifi };

    std::vector<nbt::ApCandidate> v = nbt::collectApCandidates(paths, 3);
    CHECK(v.size() == 2);
    if (v.size() == 2) {
        CHECK(v[0].path == iap2 && v[0].ap.key == "currentKey" && v[0].ap.ssid == "BMW51709 CarPlay");
        CHECK(v[1].path == wifi && v[1].ap.key == "staleKey01");
    }
    // both in sync -> one candidate
    put(wifi, "BMW51709 CarPlay", "currentKey");
    v = nbt::collectApCandidates(paths, 3);
    CHECK(v.size() == 1 && v[0].path == iap2);
    // only iap2_Wifi.cfg readable -> still works
    const char* onlyWifi[] = { missing, wifi };
    v = nbt::collectApCandidates(onlyWifi, 2);
    CHECK(v.size() == 1 && v[0].path == wifi);
    // different SSID in the fallback: its key is also tried on the live SSID
    put(wifi, "Other CarPlay", "otherKey01");
    v = nbt::collectApCandidates(paths, 3);
    CHECK(v.size() == 3 && v[1].ap.ssid == "BMW51709 CarPlay" && v[1].ap.key == "otherKey01");
    // nothing readable
    const char* none[] = { missing };
    CHECK(nbt::collectApCandidates(none, 1).empty());
    // default order: iap2.cfg first
    CHECK(nbt::kApCfgPathCount == 4 && strcmp(nbt::kApCfgPaths[0], "/var/opt/conn/carplay/iap2.cfg") == 0);
    unlink(iap2); unlink(wifi);
    printf(fails ? "test_ap_candidates: %d FAILED\n" : "test_ap_candidates: all passed\n", fails);
    return fails ? 1 : 0;
}
