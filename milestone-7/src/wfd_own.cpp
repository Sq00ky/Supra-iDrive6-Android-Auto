// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Our own passphrase on the car's Wi-Fi Direct group - see wfd_own.hpp.
#include "wfd_own.hpp"

#include "gcf_client.hpp"
#include "nbt_paths.hpp"

#include <dlfcn.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

namespace nbt {

static const char kOwnPassName[] = "wfd-own-pass";

// ---------------------------------------------------------------- child: nbt-aad --wfd-own <pass> [ssid]
static bool wfdIsOn(GcfClient& g) {
    std::string r = g.call("Wlan_ConnMngr_WFD_isWFDEnable", "interfaceName='wfd0'", 10);
    std::string v;
    return gcfResult(r) == 0 && gcfField(r, "wfd_on", v) && v == "1";
}

int runWfdOwnCommand(int argc, char** argv) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    std::string pass = argc > 2 ? argv[2] : "", ssid = argc > 3 ? argv[3] : "", dev;
    if (!wfdOwnPassUsable(pass)) {
        printf("[WfdOwn] passphrase not usable (8..63 printable ASCII, no quotes, ';', '\\\\' or braces)\n");
        printf("wfd-own-result: ok=0 ssid=\n");
        return 2;
    }
    GcfClient g;
    std::string why;
    if (!g.open("nbtaad_wfd", why)) {
        printf("[WfdOwn] cannot talk to the car's connection manager: %s\n", why.c_str());
        printf("wfd-own-result: ok=0 ssid=\n");
        return 1;
    }
    // The group's name: the car's own (the only one WFD_enable accepts, anything else -> result 2 and the group is
    // off). The connection manager's stored group always wins, also over NBT_WFD_SSID; the pin and then the ladder
    // DIRECT mode always used (persistency, iap2 name, ifconfig, mwud) only when it has none.
    {
        std::string stored;
        std::string r = g.call("Wlan_ConnMngr_WFD_getPersistedGroupList", "interfaceName='wfd0'", 10);
        if (wfdPersistedGoSsid(r, stored)) {
            printf("[WfdOwn] group name from the connection manager: '%s'%s\n", stored.c_str(),
                   !ssid.empty() && ssid != stored ? " (NBT_WFD_SSID ignored: the car accepts only its own name)" : "");
            ssid = stored;
        } else {
            printf("[WfdOwn] the connection manager has no stored group (result %d)%s\n", r.empty() ? -1 : gcfResult(r),
                   ssid.empty() ? "" : ": using NBT_WFD_SSID");
        }
    }
    if (ssid.empty() && resolveWfdSsid("wfd0", ssid))
        printf("[WfdOwn] group name from the head unit's files: '%s'\n", ssid.c_str());
    if (!wfdDeviceNameFromSsid(ssid, dev)) {
        printf("[WfdOwn] no group name of the form DIRECT-xx-<name> ('%s'): set NBT_WFD_SSID\n", ssid.c_str());
        printf("wfd-own-result: ok=0 ssid=\n");
        return 1;
    }
    bool wasOn = wfdIsOn(g);
    std::string r = g.call("Wlan_ConnMngr_WFD_disable", "interfaceName='wfd0'", 15);
    printf("[WfdOwn] Wi-Fi Direct was %s; WFD_disable: result %d%s\n", wasOn ? "on" : "off",
           r.empty() ? -1 : gcfResult(r), gcfResult(r) == 7 ? " (it was off already)" : "");
    sleep(2);
    r = g.call("Wlan_ConnMngr_WFD_enable",
               "interfaceName='wfd0' device_name='" + dev + "' ssid='" + ssid +
                   "' number_of_sec_devices=2 secondary_device_type={ 6, 4 } passphrase='" + pass + "'",
               20);
    int er = r.empty() ? -1 : gcfResult(r);
    printf("[WfdOwn] WFD_enable '%s' with nbt-aad's passphrase: result %d%s\n", ssid.c_str(), er,
           er == 0 ? "" : er == 2 ? " (refused: the name is not the car's own)" : er == -1 ? " (no reply)" : "");
    bool on = false;
    for (int i = 0; i < 20 && er == 0; ++i) {
        if ((on = wfdIsOn(g))) break;
        sleep(1);
    }
    if (er == 0) printf("[WfdOwn] Wi-Fi Direct %s\n", on ? "is on with nbt-aad's passphrase" : "did not come back on within 20 s");
    if (!on && er != 0 && wasOn) {
        // Refused: put the car's own group back if its passphrase file is usable, else it stays off until a reboot.
        ApInfo car;
        std::string used;
        if (loadWfdAp(wfdPassPaths(), wfdPassPathCount(), car, used, true)) {
            r = g.call("Wlan_ConnMngr_WFD_enable",
                       "interfaceName='wfd0' device_name='" + dev + "' ssid='" + ssid +
                           "' number_of_sec_devices=2 secondary_device_type={ 6, 4 } passphrase='" + car.key + "'",
                       20);
            printf("[WfdOwn] put the car's own group back (passphrase from %s): result %d\n", used.c_str(),
                   r.empty() ? -1 : gcfResult(r));
        } else {
            printf("[WfdOwn] Wi-Fi Direct is off until the next reboot (no usable WFDPass.conf to restore it with)\n");
        }
    }
    printf("wfd-own-result: ok=%d ssid=%s\n", on ? 1 : 0, on ? ssid.c_str() : "");
    fflush(stdout);
    _exit(on ? 0 : 1);   // the reply reader is blocked in sss_read
}

int runWfdStatusCommand() {
    setvbuf(stdout, NULL, _IOLBF, 0);
    GcfClient g;
    std::string why;
    if (!g.open("nbtaad_wst", why)) {
        printf("[WfdOwn] cannot talk to the car's connection manager: %s\n", why.c_str());
        printf("wfd-own-status: on=-1 ssid=\n");
        fflush(stdout);
        _exit(1);
    }
    std::string r = g.call("Wlan_ConnMngr_WFD_isWFDEnable", "interfaceName='wfd0'", 10), v;
    int on = (gcfResult(r) == 0 && gcfField(r, "wfd_on", v)) ? (v == "1" ? 1 : 0) : -1;
    std::string ssid;
    r = g.call("Wlan_ConnMngr_WFD_getPersistedGroupList", "interfaceName='wfd0'", 10);
    wfdPersistedGoSsid(r, ssid);
    printf("wfd-own-status: on=%d ssid=%s\n", on, ssid.c_str());
    fflush(stdout);
    _exit(0);   // the reply reader is blocked in sss_read
}

// ---------------------------------------------------------------- daemon side
static bool makeOwnPass(std::string& pass, std::string& where) {
    const char* e = getenv("NBT_WFD_PASS");
    if (e && *e) {
        if (!wfdOwnPassUsable(e)) {
            printf("[WfdOwn] NBT_WFD_PASS is not usable (8..63 printable ASCII, no quotes, ';', '\\\\' or braces): "
                   "using the stored/generated one\n");
        } else {
            pass = e;
            where = "NBT_WFD_PASS";
            return true;
        }
    }
    std::string path = dataFile(kOwnPassName).c_str(), text;
    if (readWholeFile(path.c_str(), text) && wfdOwnPassFromText(text, pass)) { where = path; return true; }
    unsigned char raw[16];
    int fd = ::open("/dev/urandom", O_RDONLY);
    ssize_t got = fd >= 0 ? read(fd, raw, sizeof(raw)) : -1;
    if (fd >= 0) close(fd);
    if (got != (ssize_t)sizeof(raw)) {
        srand((unsigned)time(NULL) ^ (unsigned)getpid());
        for (size_t i = 0; i < sizeof(raw); ++i) raw[i] = (unsigned char)rand();
    }
    pass = wfdOwnPassFromBytes(raw, sizeof(raw));
    makeDataDir();
    FILE* f = fopen(path.c_str(), "w");
    if (f) {
        fprintf(f, "%s\n", pass.c_str());
        fclose(f);
        chmod(path.c_str(), 0600);
        where = path + " (new)";
    } else {
        where = "generated, NOT stored (" + path + " not writable): a new one at the next start";
    }
    return true;
}

static bool dhcpdOnWfd0() {
    FILE* p = popen("pidin ar 2>/dev/null", "r");
    if (!p) return false;
    char line[512];
    bool found = false;
    while (fgets(line, sizeof(line), p))
        if (strstr(line, "dhcpd") && strstr(line, "wfd0")) found = true;
    pclose(p);
    return found;
}

// The binary that is running (so the child is the same build), else the install path.
static std::string selfExe() {
    std::string p;
    if (readWholeFile("/proc/self/exefile", p)) {
        p = wfdTrim(std::string(p.c_str()));
        if (!p.empty() && p[0] == '/' && access(p.c_str(), X_OK) == 0 && p.find('\'') == std::string::npos) return p;
    }
    return daemonPath();
}

bool wfdOwnStatus(WfdWatchState& st, bool gcf) {
    st = WfdWatchState();
    char line[512];
    FILE* p = NULL;
    if (gcf) {
    const std::string self = selfExe();
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "LD_LIBRARY_PATH=/opt/conn/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH} '%s' --wfd-own-status 2>&1",
             self.c_str());
    p = popen(cmd, "r");
    if (!p) return false;
    std::string out;
    while (fgets(line, sizeof(line), p)) out += line;
    pclose(p);
    if (!wfdStatusParse(out, st.on, st.ssid)) return false;
    }
    p = popen("pidin ar 2>/dev/null", "r");
    if (p) {
        std::string text;
        while (fgets(line, sizeof(line), p)) text += line;
        pclose(p);
        st.dhcpdPid = wfdDhcpdPid(text);
    }
    for (size_t i = 0; i < wfdPassPathCount(); ++i) {
        struct stat sb;
        if (stat(wfdPassPaths()[i], &sb) != 0) continue;
        char b[256];
        snprintf(b, sizeof(b), "%s %ld %ld", wfdPassPaths()[i], (long)sb.st_mtime, (long)sb.st_size);
        st.carFile = b;
        break;
    }
    return true;
}

bool wfdSsidFromConnMngr(std::string& ssid) {
    static std::string cached;
    static time_t lastTry = 0;
    static int lastLogged = -1;   // 0 = miss logged, 1 = hit logged
    if (!cached.empty()) { ssid = cached; return true; }
    time_t now = time(NULL);
    if (lastTry && now - lastTry < 30 && now >= lastTry) return false;
    lastTry = now;
    WfdWatchState st;
    std::string dn;
    bool ran = wfdOwnStatus(st, true);
    if (ran && !st.ssid.empty() && wfdDeviceNameFromSsid(st.ssid, dn)) {
        cached = ssid = st.ssid;
        if (lastLogged != 1)
            printf("[Wireless] Wi-Fi Direct SSID: '%s' (the car's connection manager, getPersistedGroupList)\n",
                   ssid.c_str());
        lastLogged = 1;
        return true;
    }
    if (lastLogged != 0)
        printf("[Wireless] Wi-Fi Direct SSID: the car's connection manager %s (asked again every 30 s)\n",
               !ran || st.on < 0 ? "could not be asked (no answer from it; the status child could not register or "
                                   "isWFDEnable failed)"
               : st.on == 0     ? "answered: Wi-Fi Direct is OFF and no group name is stored"
                                : "answered: Wi-Fi Direct is on, but no group name is stored");
    lastLogged = 0;
    return false;
}

int runLongVinCommand() {
    setvbuf(stdout, NULL, _IOLBF, 0);
    const char* lib = getenv("NBT_PALCODING");
    if (!lib || !*lib) lib = "/opt/speech/lib/libPalCoding.so";
    if (access(lib, R_OK) != 0) {
        printf("long-vin: ok=0 vin= why=%s not on this head unit\n", lib);
        return 1;
    }
    void* h = dlopen(lib, RTLD_NOW | RTLD_LOCAL);
    if (!h) {
        const char* e = dlerror();
        printf("long-vin: ok=0 vin= why=dlopen %s failed: %s\n", lib, e ? e : "?");
        return 1;
    }
    typedef unsigned char (*GetLongVin)(char*);
    GetLongVin fn = (GetLongVin)dlsym(h, "get_long_vin");
    if (!fn) {
        printf("long-vin: ok=0 vin= why=%s has no get_long_vin\n", lib);
        return 1;
    }
    // Writes exactly 17 bytes without a terminator (libPalCoding 18-03: getBuffer(key, out, 0x11, &len), len == 17).
    char buf[64];
    memset(buf, 0, sizeof(buf));
    unsigned char ok = fn(buf);
    buf[17] = 0;
    std::string v(buf);
    printf("long-vin: ok=%d vin=%s why=%s\n", ok && wfdVinValid(v) ? 1 : 0, wfdVinValid(v) ? v.c_str() : "",
           !ok ? "get_long_vin returned 0 (coding page not attached or no long VIN coded)"
               : wfdVinValid(v) ? "get_long_vin" : "get_long_vin returned no 17-character VIN");
    fflush(stdout);
    _exit(ok && wfdVinValid(v) ? 0 : 1);
}

static bool readLongVin(std::string& vin, std::string& why) {
    const std::string self = selfExe();
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "'%s' --long-vin 2>&1", self.c_str());
    FILE* p = popen(cmd, "r");
    if (!p) { why = "cannot start the --long-vin child"; return false; }
    std::string out;
    char line[512];
    while (fgets(line, sizeof(line), p)) out += line;
    int st = pclose(p);
    if (!wfdLongVinParse(out, vin, why)) {
        char b[96];
        snprintf(b, sizeof(b), "the --long-vin child printed no result (exit status 0x%x: crashed?)", st);
        why = b;
        return false;
    }
    return !vin.empty();
}

bool wfdSsidFromVin(std::string& ssid) {
    static bool done = false, haveIt = false;
    static std::string cached;
    if (done) { ssid = cached; return haveIt; }
    done = true;
    std::string vin, why;
    bool gotVin = readLongVin(vin, why);
    std::string brand, brandFrom;
    const char* inis[] = { "/var/opt/conn/carplay/carplay.ini", "/fs/sda1/opt/conn/carplay/carplay.ini" };
    for (int i = 0; i < 2 && brand.empty(); ++i) {
        std::string text;
        if (readWholeFile(inis[i], text)) {
            brand = wfdIniManufacturer(text);
            if (!brand.empty()) brandFrom = inis[i];
        }
    }
    if (brand.empty() && gotVin) {
        brand = wfdBrandFromWmi(vin);
        if (!brand.empty()) brandFrom = "the VIN's manufacturer code " + vin.substr(0, 3);
    }
    if (!gotVin) {
        printf("[Vin] no VIN from the car's coding: %s\n", why.c_str());
    } else {
        printf("[Vin] VIN %s (17 characters, libPalCoding get_long_vin); brand %s\n", wfdVinShort(vin).c_str(),
               brand.empty() ? "unknown (no carplay.ini, manufacturer code not in nbt-aad's list)"
                             : ("'" + brand + "' from " + brandFrom).c_str());
    }
    cached = wfdSsidFromVinBrand(vin, brand);
    haveIt = !cached.empty();
    if (haveIt) printf("[Vin] Wi-Fi Direct group name built from them: '%s'\n", cached.c_str());
    fflush(stdout);
    ssid = cached;
    return haveIt;
}

bool applyWfdOwnPass(ApInfo& ap, std::string& passPath) {
    std::string pass;
    makeOwnPass(pass, passPath);
    const char* pin = getenv("NBT_WFD_SSID");
    if (pin && strchr(pin, '\'')) pin = NULL;
    // No pin: the name built from the VIN, used by the child only when the connection manager has no stored group.
    std::string derived;
    if ((!pin || !*pin) && wfdSsidFromVin(derived) && derived.find('\'') == std::string::npos) pin = derived.c_str();
    const std::string self = selfExe();
    char cmd[640];
    // The car's libraries resolve through /opt/conn/lib; LD_PRELOAD is already gone for children (main()).
    snprintf(cmd, sizeof(cmd), "LD_LIBRARY_PATH=/opt/conn/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH} '%s' --wfd-own '%s'%s%s%s 2>&1",
             self.c_str(), pass.c_str(), pin && *pin ? " '" : "", pin && *pin ? pin : "", pin && *pin ? "'" : "");
    printf("[WfdOwn] setting nbt-aad's own passphrase on the car's Wi-Fi Direct group (%s)\n", passPath.c_str());
    fflush(stdout);
    FILE* p = popen(cmd, "r");
    if (!p) { printf("[WfdOwn] cannot start '%s --wfd-own'\n", self.c_str()); return false; }
    std::string out;
    char line[512];
    while (fgets(line, sizeof(line), p)) {
        out += line;
        if (strncmp(line, "wfd-own-result:", 15) != 0) fputs(line, stdout);   // the child's progress lines, verbatim
    }
    pclose(p);
    bool ok = false;
    std::string ssid;
    if (!wfdOwnParseResult(out, ok, ssid)) {
        printf("[WfdOwn] '%s --wfd-own' printed no result line\n", self.c_str());
        return false;
    }
    if (!ok || ssid.empty()) return false;
    // The car restarts dhcpd on wfd0 itself after the enable; offer the group once it is serving addresses.
    bool dhcp = false;
    for (int i = 0; i < 15 && !(dhcp = dhcpdOnWfd0()); ++i) sleep(1);
    printf("[WfdOwn] DHCP on wfd0: %s\n", dhcp ? "running" : "not seen within 15 s (offering the group anyway)");
    ap.ssid = ssid;
    ap.key = pass;
    ap.security = 8;   // WPA2_PERSONAL
    ap.apType = 1;     // DYNAMIC
    return true;
}

} // namespace nbt
