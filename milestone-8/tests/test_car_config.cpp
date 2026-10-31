// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Host test: car_config.hpp (NBT_SCREEN / NBT_VEHICLE / carplay.ini) and NBT_GUIDANCE_BT channel 8 retype.
#include "car_config.hpp"
#include "sdr_builder.hpp"
#include "dhu_sdp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

using namespace nbt;

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

static void writeFile(const char* path, const char* text) {
    FILE* f = fopen(path, "w");
    if (f) { fputs(text, f); fclose(f); }
}

static bool getV(const std::vector<uint8_t>& p, size_t end, size_t& pos, unsigned long long& v) {
    v = 0; int sh = 0;
    while (pos < end && sh < 64) { uint8_t b = p[pos++]; v |= (unsigned long long)(b & 0x7f) << sh; if (!(b & 0x80)) return true; sh += 7; }
    return false;
}

// audio_type (MediaSinkService field 2) of the channel `ch` descriptor, -1 if absent.
static int audioType(const std::vector<uint8_t>& p, int ch) {
    size_t pos = 0;
    while (pos < p.size()) {
        unsigned long long k, v;
        if (!getV(p, p.size(), pos, k)) return -2;
        int f = (int)(k >> 3), wt = (int)(k & 7);
        if (wt == 0) { if (!getV(p, p.size(), pos, v)) return -2; continue; }
        if (wt != 2) return -2;
        if (!getV(p, p.size(), pos, v)) return -2;
        size_t body = pos, end = pos + (size_t)v;
        pos = end;
        if (f != 1) continue;
        size_t q = body;
        unsigned long long k2, cid;
        if (!getV(p, end, q, k2) || k2 != 8 || !getV(p, end, q, cid) || (int)cid != ch) continue;
        while (q < end) {
            unsigned long long k3, l3;
            if (!getV(p, end, q, k3)) return -2;
            if ((k3 & 7) == 0) { if (!getV(p, end, q, l3)) return -2; continue; }
            if ((k3 & 7) != 2 || !getV(p, end, q, l3)) return -2;
            size_t sb = q, se = q + (size_t)l3;
            q = se;
            if ((k3 >> 3) != 3) continue;
            size_t r = sb;
            while (r < se) {
                unsigned long long k4, v4;
                if (!getV(p, se, r, k4)) return -2;
                if ((k4 & 7) == 0) { if (!getV(p, se, r, v4)) return -2; if ((k4 >> 3) == 2) return (int)v4; }
                else if ((k4 & 7) == 2) { if (!getV(p, se, r, v4)) return -2; r += (size_t)v4; }
                else return -2;
            }
        }
    }
    return -1;
}

int main() {
    const char* toyota = "#Version: 1.10\n#Coded is: def,1280_480,touch_screen\n[General]\r\nModel=J29-NBTEvo-0319\r\n"
                         "Manufacturer=Toyota\r\n[Screen]\r\nWidth=1280\r\nHeight=480\r\nWindowName=1280\r\n";
    const char* bmw = "[General]\nModel=F20-NBTEvo-0716\nManufacturer=BMW\n[Screen]\nWidth=1440\nHeight=540\n"
                      "[Screen]\nWidth=800\nHeight=480\n";
    writeFile("/tmp/nbt_cfg_toyota.ini", toyota);
    writeFile("/tmp/nbt_cfg_bmw.ini", bmw);
    unsetenv("NBT_SCREEN"); unsetenv("NBT_VEHICLE");

    // Toyota carplay.ini (same content as the car's generated file)
    setenv("NBT_CARPLAY_INI", "/tmp/nbt_cfg_toyota.ini", 1);
    CarConfig c = carcfg_detail::load();
    CHECK(c.screenW == 1280 && c.screenH == 480);
    CHECK(!c.bmw);
    CHECK(strcmp(c.screenFrom, "carplay.ini") == 0 && strcmp(c.vehicleFrom, "carplay.ini") == 0 && c.make == MAKE_TOYOTA);
    CHECK(c.coded == "def,1280_480,touch_screen");
    CHECK(c.model == "J29-NBTEvo-0319");

    // BMW 10.25": first [Screen] only
    setenv("NBT_CARPLAY_INI", "/tmp/nbt_cfg_bmw.ini", 1);
    c = carcfg_detail::load();
    CHECK(c.screenW == 1440 && c.screenH == 540);
    CHECK(c.bmw);

    // env wins over the file
    setenv("NBT_SCREEN", "1280x480", 1);
    setenv("NBT_VEHICLE", "Toyota", 1);
    c = carcfg_detail::load();
    CHECK(c.screenW == 1280 && c.screenH == 480 && !c.bmw);
    CHECK(strcmp(c.screenFrom, "NBT_SCREEN") == 0 && strcmp(c.vehicleFrom, "NBT_VEHICLE") == 0);

    // bad values are ignored
    setenv("NBT_SCREEN", "huge", 1);
    setenv("NBT_VEHICLE", "audi", 1);
    c = carcfg_detail::load();
    CHECK(c.screenW == 1440 && c.bmw);

    // MINI / Rolls-Royce (2026-10-27): the CarPlay logo Connectivity wrote wins over Manufacturer (a MINI keeps BMW)
    unsetenv("NBT_SCREEN"); unsetenv("NBT_VEHICLE");
    writeFile("/tmp/nbt_cfg_mini.ini", "[General]\nModel=F56-NBTEvo-0716\nManufacturer=BMW\n"
                                       "OEMIcon=/opt/conn/data/carplay/OEM_MINI.png\nOEMIconLabel=MINI\n[Screen]\nWidth=1280\nHeight=480\n");
    setenv("NBT_CARPLAY_INI", "/tmp/nbt_cfg_mini.ini", 1);
    c = carcfg_detail::load();
    CHECK(c.make == MAKE_MINI && c.bmw && c.oemIcon == "OEM_MINI.png");
    writeFile("/tmp/nbt_cfg_rr.ini", "[General]\nModel=RR11-NBTEvo-0716\nManufacturer=Rolls-Royce\n"
              "OEMIconSet=Path:/opt/conn/data/carplay/OEM_RR.png;Width:120;Height:120;PreRendered:NO\n[Screen]\nWidth=1440\nHeight=540\n");
    setenv("NBT_CARPLAY_INI", "/tmp/nbt_cfg_rr.ini", 1);
    c = carcfg_detail::load();
    CHECK(c.make == MAKE_ROLLS_ROYCE && c.oemIcon == "OEM_RR.png");
    writeFile("/tmp/nbt_cfg_mini2.ini", "[General]\nManufacturer=MINI\n[Screen]\nWidth=1180\nHeight=480\n");
    setenv("NBT_CARPLAY_INI", "/tmp/nbt_cfg_mini2.ini", 1);
    c = carcfg_detail::load();
    CHECK(c.make == MAKE_MINI);                                       // no logo line: Manufacturer
    CHECK(c.screenW == 1280 && c.screenH == 480 && c.iniW == 1180);    // 1180_480 coding = 1280x480 panel
    CHECK(miniRoundPanel(c));                                         // MINI + 1180_480: round-bezel DPI / insets
    setenv("NBT_VEHICLE", "rolls-royce", 1);
    c = carcfg_detail::load();
    CHECK(c.make == MAKE_ROLLS_ROYCE && strcmp(c.vehicleFrom, "NBT_VEHICLE") == 0);
    setenv("NBT_CARPLAY_INI", "/tmp/nbt_cfg_toyota.ini", 1);
    setenv("NBT_VEHICLE", "mini", 1);
    c = carcfg_detail::load();
    CHECK(c.make == MAKE_MINI && c.bmw);

    // no file, no env: Toyota 1280x480 defaults
    unsetenv("NBT_SCREEN"); unsetenv("NBT_VEHICLE");
    setenv("NBT_CARPLAY_INI", "/tmp/does-not-exist.ini", 1);
    c = carcfg_detail::load();
    CHECK(!c.bmw && c.screenW == 1280 && c.screenH == 480);
    CHECK(strcmp(c.vehicleFrom, "default") == 0);

    // Process-wide config for the rest: BMW 1440x540 from the ini
    setenv("NBT_CARPLAY_INI", "/tmp/nbt_cfg_bmw.ini", 1);
    CHECK(carConfig().bmw && carConfig().screenW == 1440);
    CHECK(canvasRowsShown() == 480);   // 540 * 1280 / 1440
    CHECK(mmrDstHeight() == 810);      // 720 * 1440 / 1280
    SdrIdentity id;
    CHECK(id.carMake == "BMW" && id.carModel == "F20" && id.huMake == "BMW");

    // NBT_GUIDANCE_BT=1: channel 8 GUIDANCE(1) -> SYSTEM_AUDIO(2), nothing else changes
    std::vector<uint8_t> sdr(kDhuSdpProtobuf, kDhuSdpProtobuf + kDhuSdpProtobufLen);
    CHECK(audioType(sdr, 8) == 1);
    int before9 = audioType(sdr, 9);
    unsetenv("NBT_GUIDANCE_BT"); setenv("NBT_AUDIO_BT", "0", 1);
    CHECK(retypeGuidanceSink(sdr) == 0 && audioType(sdr, 8) == 1);   // A2I mode: GUIDANCE kept
    unsetenv("NBT_AUDIO_BT");
    { std::vector<uint8_t> t = sdr; CHECK(retypeGuidanceSink(t) == 1 && audioType(t, 8) == 2); }   // unset = BT mode
    setenv("NBT_AUDIO_BT", "1", 1); setenv("NBT_GUIDANCE_BT", "0", 1);
    CHECK(retypeGuidanceSink(sdr) == 0 && audioType(sdr, 8) == 1);   // opt-out in BT mode
    unsetenv("NBT_GUIDANCE_BT");
    { std::vector<uint8_t> t = sdr; CHECK(retypeGuidanceSink(t) == 1 && audioType(t, 8) == 2); }   // BT default on
    setenv("NBT_AUDIO_BT", "0", 1);
    setenv("NBT_GUIDANCE_BT", "1", 1);
    std::vector<uint8_t> copy = sdr;
    CHECK(retypeGuidanceSink(sdr) == 1);
    CHECK(audioType(sdr, 8) == 2);
    CHECK(audioType(sdr, 9) == before9);
    CHECK(sdr.size() == copy.size());
    size_t diffs = 0;
    for (size_t i = 0; i < sdr.size(); ++i) if (sdr[i] != copy[i]) ++diffs;
    CHECK(diffs == 1);

    printf(fails ? "test_car_config FAILED (%d)\n" : "test_car_config OK\n", fails);
    return fails ? 1 : 0;
}
