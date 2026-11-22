// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// vinprobe: can this head unit's VIN be read without the car's diagnostics? Read-only, sends nothing anywhere.
// Tries every source the derived Wi-Fi Direct name uses (alt_wifi.hpp wfdVinTail) plus a few more, prints what each
// one gave, and the name nbt-aad would derive for each brand.
//   usage: vinprobe [-q]     -q = mask the VIN's middle characters (for posting the output publicly)
#include "../src/alt_wifi.hpp"
#include "../src/wfd_own.hpp"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

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

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i)
        if (!strcmp(argv[i], "-q")) g_mask = true;
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
    if (resolveWfdSsid("wfd0", ssid)) printf("  resolved: '%s'\n", show(ssid).c_str());
    else printf("  not resolved from the head unit's files\n");
    std::string tail, from;
    if (!wfdVinTail(tail, from)) {
        printf("RESULT: no VIN from any source nbt-aad uses\n");
        return 1;
    }
    printf("RESULT: VIN tail %s from %s\n", show(tail).c_str(), from.c_str());
    static const char* const kBrands[] = { "BMW", "MINI", "Toyota", "Rolls-Royce" };
    for (size_t i = 0; i < 4; ++i)
        printf("  derived name for %-11s %s%s\n", kBrands[i], show(wfdSsidFromBrandVin(kBrands[i], tail)).c_str(),
               !ssid.empty() && wfdSsidFromBrandVin(kBrands[i], tail) == ssid ? "   <- matches the car's name" : "");
    return 0;
}
