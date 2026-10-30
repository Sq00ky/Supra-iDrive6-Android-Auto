// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef CAR_CONFIG_HPP
#define CAR_CONFIG_HPP

// Vehicle and display profile, read once at start-up.
//
// Source order (first hit wins, per value):
//   1. environment:  NBT_SCREEN=<w>x<h>   NBT_VEHICLE=toyota|bmw|mini|rollsroyce
//   2. the car's own CarPlay config, the file Connectivity writes from the vehicle coding
//      (/var/opt/conn/carplay/carplay.ini, header "#Coded is: def,1280_480,touch_screen"):
//      [Screen] Width / Height and [General] Manufacturer / Model, and the brand logo Connectivity picked for CarPlay
//      (OEMIcon=/opt/conn/data/carplay/OEM_MINI.png or OEMIconSet=Path:.../OEM_RR.png;...; files OEM_BMW, OEM_BMWi,
//      OEM_MINI, OEM_RR, OEM_TOYOTA). The logo wins over Manufacturer: it is the brand the car itself shows.
//   3. defaults: 1280x480, Toyota (GR Supra J29)
// NBT_CARPLAY_INI=<path> points at a different file. The factory TEMPLATE
// /opt/conn/data/carplay/carplay.ini is never used: it lists every screen option (900x540 first).

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
namespace nbt {

enum CarMake { MAKE_TOYOTA = 0, MAKE_BMW, MAKE_MINI, MAKE_ROLLS_ROYCE };

inline const char* carMakeName(int m) {
    switch (m) {
    case MAKE_BMW: return "BMW";
    case MAKE_MINI: return "MINI";
    case MAKE_ROLLS_ROYCE: return "Rolls-Royce";
    default: return "Toyota";
    }
}

struct CarConfig {
    int screenW, screenH;
    int make;                   // CarMake
    int iniW, iniH;             // carplay.ini [Screen] size (the factory CarPlay picture), 0 if none
    int dispW, dispH;           // QNX Screen display 0 size, 0 if unknown
    bool bmw;                   // make != MAKE_TOYOTA (BMW group head unit identity)
    std::string iniPath;        // empty = no carplay.ini found
    std::string manufacturer;   // [General] Manufacturer
    std::string model;          // [General] Model, e.g. J29-NBTEvo-0319
    std::string coded;          // "#Coded is: ..." line
    std::string oemIcon;        // OEMIcon / OEMIconSet logo file, e.g. OEM_MINI.png
    const char* screenFrom;     // "NBT_SCREEN", "carplay.ini", "default"
    const char* vehicleFrom;    // "NBT_VEHICLE", "carplay.ini", "default"
};

namespace carcfg_detail {

inline std::string trim(const char* s) {
    while (*s == ' ' || *s == '\t') ++s;
    std::string r(s);
    while (!r.empty() && (r[r.size() - 1] == '\r' || r[r.size() - 1] == '\n' || r[r.size() - 1] == ' ' ||
                          r[r.size() - 1] == '\t'))
        r.erase(r.size() - 1);
    return r;
}

inline bool ieq(const std::string& a, const char* b) {
    size_t n = strlen(b);
    if (a.size() != n) return false;
    for (size_t i = 0; i < n; ++i) {
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z') x = (char)(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = (char)(y - 'A' + 'a');
        if (x != y) return false;
    }
    return true;
}

// Manufacturer string or logo file name -> CarMake, -1 if unknown.
inline int makeFromText(const std::string& s0) {
    std::string s;
    for (size_t i = 0; i < s0.size(); ++i) {
        char ch = s0[i];
        if (ch >= 'A' && ch <= 'Z') ch = (char)(ch - 'A' + 'a');
        if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9')) s += ch;    // drop '-', ' ', '_', '.'
    }
    if (s.find("rollsroyce") != std::string::npos || s == "rr" || s.find("oemrr") != std::string::npos) return MAKE_ROLLS_ROYCE;
    if (s.find("mini") != std::string::npos) return MAKE_MINI;
    if (s.find("toyota") != std::string::npos) return MAKE_TOYOTA;
    if (s.find("bmw") != std::string::npos) return MAKE_BMW;                // BMW, BMWi, OEM_BMW(i).png
    return -1;
}

// "Path:/opt/conn/data/carplay/OEM_RR.png;Width:120;..." or a plain path -> "OEM_RR.png".
inline std::string iconFile(const std::string& v) {
    size_t e = v.find(".png");
    if (e == std::string::npos) return std::string();
    size_t b = v.rfind('/', e);
    b = (b == std::string::npos) ? 0 : b + 1;
    size_t c = v.rfind(':', e);
    if (c != std::string::npos && c + 1 > b) b = c + 1;
    return v.substr(b, e + 4 - b);
}

inline bool sane(int w, int h) { return w >= 320 && w <= 4096 && h >= 200 && h <= 2160; }

// Parses one carplay.ini. Only the FIRST [Screen] section counts. Returns false if the file can't be read.
inline bool parseIni(const char* path, CarConfig& c, int& w, int& h) {
    FILE* f = fopen(path, "r");
    if (!f) return false;
    char line[512];
    std::string section;
    int screens = 0;
    w = h = 0;
    while (fgets(line, sizeof(line), f)) {
        std::string t = trim(line);
        if (t.empty()) continue;
        if (t[0] == '#') {
            if (t.compare(0, 10, "#Coded is:") == 0) c.coded = trim(t.c_str() + 10);
            continue;
        }
        if (t[0] == '[') {
            section = t;
            if (section == "[Screen]") ++screens;
            continue;
        }
        size_t eq = t.find('=');
        if (eq == std::string::npos) continue;
        std::string k = trim(t.substr(0, eq).c_str()), v = trim(t.substr(eq + 1).c_str());
        if ((k == "OEMIcon" || k == "OEMIconSet") && c.oemIcon.empty()) c.oemIcon = iconFile(v);
        if (section == "[General]") {
            if (k == "Manufacturer" && c.manufacturer.empty()) c.manufacturer = v;
            else if (k == "Model" && c.model.empty()) c.model = v;
        } else if (section == "[Screen]" && screens == 1) {
            if (k == "Width") w = atoi(v.c_str());
            else if (k == "Height") h = atoi(v.c_str());
        }
    }
    fclose(f);
    return true;
}

// Pulls "x=<w>" and "y=<h>" out of LayerManagerControl's "- resolution:           x=1280, y=480" line.
inline bool parseResolutionLine(const char* line, int& w, int& h) {
    const char* r = strstr(line, "resolution:");
    if (!r) return false;
    const char* x = strstr(r, "x=");
    const char* y = x ? strstr(x, "y=") : NULL;
    if (!x || !y) return false;
    w = atoi(x + 2); h = atoi(y + 2);
    return sane(w, h);
}

// Size of screen 0 (the central display) as LayerManager reports it (`LayerManagerControl get screen 0`). 0 = unknown.
inline void displaySize(int& w, int& h) {
    w = h = 0;
    const char* e = getenv("NBT_DISPLAY_SIZE");          // host tests / manual override of the probe
    if (e && *e) { if (sscanf(e, "%dx%d", &w, &h) != 2) w = h = 0; return; }
#ifdef __QNX__
    FILE* p = popen("/opt/sys/bin/LayerManagerControl get screen 0 2>/dev/null", "r");
    if (!p) return;
    char line[256];
    while (fgets(line, sizeof(line), p)) {
        int a = 0, b = 0;
        if (!w && parseResolutionLine(line, a, b)) { w = a; h = b; }
    }
    pclose(p);
#endif
}

// The carplay.ini coding is the factory CarPlay PICTURE size, which on some cars is smaller than the panel:
// 1180_480 on MINI, and a non-full-screen CarPlay coding (picture on the left ~2/3, iDrive's split screen on the
// right) on some F-series NBT Evo cars, where Android Auto then filled only that part of the screen (user photo,
// 2026-10-29). When the display is at least as large as the coded picture in both directions, use the display.
inline void applyPanel(CarConfig& c, int dw, int dh) {
    if (!sane(dw, dh) || !c.iniW || !c.iniH) return;
    if (dw >= c.iniW && dh >= c.iniH && (dw != c.iniW || dh != c.iniH)) {
        c.screenW = dw; c.screenH = dh;
        c.screenFrom = "display size (carplay.ini codes a smaller CarPlay picture)";
    }
}

inline CarConfig load() {
    CarConfig c;
    c.screenW = 1280; c.screenH = 480; c.make = MAKE_TOYOTA; c.bmw = false; c.iniW = c.iniH = 0; c.dispW = c.dispH = 0;
    c.screenFrom = "default"; c.vehicleFrom = "default";

    int iw = 0, ih = 0;
    const char* forced = getenv("NBT_CARPLAY_INI");
    const char* paths[3] = { forced && *forced ? forced : NULL,
                             "/var/opt/conn/carplay/carplay.ini",
                             "/fs/sda1/opt/conn/carplay/carplay.ini" };
    for (int i = 0; i < 3; ++i) {
        if (!paths[i]) continue;
        CarConfig t = c;
        int w = 0, h = 0;
        if (parseIni(paths[i], t, w, h)) { c = t; c.iniPath = paths[i]; iw = w; ih = h; break; }
    }
    if (sane(iw, ih)) { c.screenW = iw; c.screenH = ih; c.iniW = iw; c.iniH = ih; c.screenFrom = "carplay.ini"; }
    // A 1180_480 coding (MINI F56 and others) is the CarPlay picture size; the panel itself is 1280x480 (MINI tester:
    // NBT_SCREEN=1280x480 fills the screen). iniW/iniH keep 1180x480 for the CarPlay geometry. NBT_SCREEN overrides.
    if (iw == 1180 && ih == 480) { c.screenW = 1280; c.screenH = 480; c.screenFrom = "carplay.ini 1180_480 coding = 1280x480 panel"; }
    {
        int dw = 0, dh = 0;
        displaySize(dw, dh);
        c.dispW = dw; c.dispH = dh;
        applyPanel(c, dw, dh);
    }
    {
        int byIcon = c.oemIcon.empty() ? -1 : makeFromText(c.oemIcon);
        int byMfr = c.manufacturer.empty() ? -1 : makeFromText(c.manufacturer);
        if (byIcon >= 0) { c.make = byIcon; c.vehicleFrom = "carplay.ini logo"; }
        else if (byMfr >= 0) { c.make = byMfr; c.vehicleFrom = "carplay.ini"; }
        else if (!c.manufacturer.empty()) { c.make = MAKE_BMW; c.vehicleFrom = "carplay.ini (unknown maker: BMW)"; }
    }

    const char* s = getenv("NBT_SCREEN");
    if (s && *s) {
        int w = 0, h = 0;
        if (sscanf(s, "%dx%d", &w, &h) == 2 && sane(w, h)) { c.screenW = w; c.screenH = h; c.screenFrom = "NBT_SCREEN"; }
        else printf("[Car] NBT_SCREEN='%s' ignored (expected e.g. 1440x540)\n", s);
    }
    const char* v = getenv("NBT_VEHICLE");
    if (v && *v) {
        std::string vs(v);
        int m = makeFromText(vs);
        if (m >= 0) { c.make = m; c.vehicleFrom = "NBT_VEHICLE"; }
        else printf("[Car] NBT_VEHICLE='%s' ignored (toyota, bmw, mini or rollsroyce)\n", v);
    }
    c.bmw = c.make != MAKE_TOYOTA;
    return c;
}

} // namespace carcfg_detail

// Parsed on first use. Call it once from main() before any thread starts.
inline const CarConfig& carConfig() {
    static CarConfig c = carcfg_detail::load();
    return c;
}

// The phone always renders a 1280x720 canvas (crop mode). Two ways to put it on the panel:
//   scaled (default): the canvas is scaled to the panel width; the panel shows the top rows that fit
//     (1280x480: 1:1, 1440x540: x1.125, 1180x480: x0.922 -> 520 rows).
//   native (NBT_VID_NATIVE=1, panels up to 1280x720 only, EXPERIMENTAL): the canvas is shown 1:1 from its top-left
//     corner and the phone is told (insets) to keep its UI in the top-left panel-sized area. Pixel-exact on a
//     narrower panel such as 1180x480 (no scaling blur, touch 1:1). On 1280x480 both modes are identical.
inline bool vidNative() {
    const char* e = getenv("NBT_VID_NATIVE");
    const CarConfig& c = carConfig();
    return e && e[0] == '1' && c.screenW <= 1280 && c.screenH <= 720;
}
// Canvas rows / columns the panel displays.
inline int canvasRowsShown() {
    const CarConfig& c = carConfig();
    if (vidNative()) return c.screenH;
    int rows = c.screenH * 1280 / c.screenW;
    return rows > 720 ? 720 : rows;
}
inline int canvasColsShown() { return vidNative() ? carConfig().screenW : 1280; }
// MM-Renderer destination rectangle (the part beyond the panel is clipped, like the bottom rows on the Supra).
inline int mmrDstWidth() { return vidNative() ? 1280 : carConfig().screenW; }
inline int mmrDstHeight() { return vidNative() ? 720 : 720 * carConfig().screenW / 1280; }
// Panel pixels -> canvas pixels.
inline double touchScale() { return vidNative() ? 1.0 : 1280.0 / (double)carConfig().screenW; }
// MINI round central display = carplay.ini 1180_480 coding on a MINI. The round bezel covers about 40 px at each side
// of the 1280x480 panel (MINI F60 tester 2026-10-28: NBT_VID_DPI=152 + NBT_VID_INSETS=0,240,40,40 fit, 60 was too much).
inline bool miniRoundPanel(const CarConfig& c) { return c.make == MAKE_MINI && c.iniW == 1180 && c.iniH == 480; }
inline bool miniRoundPanel() { return miniRoundPanel(carConfig()); }
// Default touch x offset in canvas pixels (NBT_TOUCH_OX overrides). MINI F60 round display (1180_480 coding): touches
// landed ~20-30 px right of the finger; -20 fixed it (tester, v1.8.35, 2026-10-29). 0 on every other car.
inline double defaultTouchOffsetX(const CarConfig& c) { return miniRoundPanel(c) ? -20.0 : 0.0; }

} // namespace nbt

#endif // CAR_CONFIG_HPP
