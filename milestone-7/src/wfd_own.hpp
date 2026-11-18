// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Our own passphrase on the car's Wi-Fi Direct group (Use WiFi Direct Network / NBT_ALT_WIFI=DIRECT).
//
// Not every car has a usable /var/opt/conn/WFDPass.conf, and the group's passphrase cannot be read anywhere else (mwud
// keeps it in memory, its log is off, its control port is closed). So in DIRECT mode nbt-aad sets the passphrase
// itself, through the car's own connection manager (car-proven on the J29, 2026-11-16, tools/aa_ap):
//   Wlan_ConnMngr_WFD_disable  interfaceName='wfd0'                                   -> result=0
//   Wlan_ConnMngr_WFD_enable   interfaceName='wfd0' device_name='<brand><last5>' ssid='DIRECT-03-<brand><last5>'
//                              number_of_sec_devices=2 secondary_device_type={ 6, 4 } passphrase='<ours>'  -> result=0
//   Wlan_ConnMngr_WFD_isWFDEnable -> wfd_on=1 at once; the car restarts its own dhcpd on wfd0 by itself.
// The name must stay the car's own (any other device_name/ssid -> result=2); only the passphrase is ours. The car's
// WFDPass.conf is left as it is, so the phone must be given OUR passphrase, never the file's. A reboot gives the car
// its own group back; nbt-aad applies ours again at every start.
//
// The GCF calls need the car's libraries (LD_LIBRARY_PATH=/opt/conn/lib), so they run in a one-shot child,
// `nbt-aad --wfd-own <passphrase>`, which prints `wfd-own-result: ok=<0|1> ssid=<ssid>` as its last line.
#ifndef WFD_OWN_HPP
#define WFD_OWN_HPP

#include "alt_wifi.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <string>

namespace nbt {

// NBT_WFD_OWN_PASS:
//   unset / auto  (default) only when the car has no usable WFDPass.conf; a usable file is used as before and the
//                 car's group is never touched
//   1 / on / yes  always (to test it on a car that does have the file)
//   0 / off / no  never: WFDPass.conf only, as before v1.9.85
enum WfdOwnPassMode { WFD_OWN_NEVER = 0, WFD_OWN_ALWAYS = 1, WFD_OWN_AUTO = 2 };
inline WfdOwnPassMode wfdOwnPassSetting(const char* env) {
    if (!env || !*env || strcasecmp(env, "auto") == 0) return WFD_OWN_AUTO;
    if (strcmp(env, "0") == 0 || strcasecmp(env, "off") == 0 || strcasecmp(env, "no") == 0 ||
        strcasecmp(env, "false") == 0)
        return WFD_OWN_NEVER;
    return WFD_OWN_ALWAYS;
}
inline WfdOwnPassMode wfdOwnPassMode() { return wfdOwnPassSetting(getenv("NBT_WFD_OWN_PASS")); }
inline const char* wfdOwnPassModeName(WfdOwnPassMode m) {
    return m == WFD_OWN_NEVER ? "never" : m == WFD_OWN_ALWAYS ? "always" : "auto (only without a usable WFDPass.conf)";
}
// Pure decision (host-tested): set our own passphrase now? `carFileUsable` = WFDPass.conf decodes to a passphrase.
inline bool wfdOwnPassDecide(WfdOwnPassMode m, bool carFileUsable) {
    return m == WFD_OWN_ALWAYS || (m == WFD_OWN_AUTO && !carFileUsable);
}

// Characters a generated passphrase uses: no 0/O/1/l/I, so it can be typed on a phone by hand.
inline const char* wfdOwnPassAlphabet() { return "abcdefghijkmnpqrstuvwxyzABCDEFGHJKLMNPQRSTUVWXYZ23456789"; }

// 16 characters from `raw` (any bytes, e.g. /dev/urandom); shorter input repeats.
inline std::string wfdOwnPassFromBytes(const unsigned char* raw, size_t n) {
    const char* abc = wfdOwnPassAlphabet();
    const size_t k = strlen(abc);
    std::string p;
    for (size_t i = 0; i < 16; ++i) p.push_back(abc[n ? raw[i % n] % k : i % k]);
    return p;
}

// A passphrase we may put into a GCF call and hand to a phone: WPA2 rule (8..63 printable ASCII) and no character
// that would end or break the '...' value in the frame.
inline bool wfdOwnPassUsable(const std::string& p) {
    if (!wfdPassphraseOk(p)) return false;
    return p.find_first_of("'\";\\{}") == std::string::npos;
}

// Text of the stored passphrase file (one line, whitespace trimmed) -> passphrase, if usable.
inline bool wfdOwnPassFromText(const std::string& text, std::string& pass) {
    std::string t = wfdTrim(text);
    size_t nl = t.find_first_of("\r\n");
    if (nl != std::string::npos) t = wfdTrim(t.substr(0, nl));
    if (!wfdOwnPassUsable(t)) return false;
    pass = t;
    return true;
}

// "DIRECT-03-Toyota51807" -> "Toyota51807" (the device_name WFD_enable wants beside it). False if not that shape.
inline bool wfdDeviceNameFromSsid(const std::string& ssid, std::string& name) {
    static const char kPre[] = "DIRECT-";
    if (ssid.compare(0, 7, kPre) != 0 || ssid.size() < 11 || ssid[9] != '-') return false;
    name = ssid.substr(10);
    if (name.empty() || name.find_first_of("'\";\\{} ") != std::string::npos) return false;
    return true;
}

// Wlan_ConnMngr_WFD_getPersistedGroupList reply -> the first group whose role is GO and whose ssid is DIRECT-..:
//   persistedGroup_List={ { index=0, role='GO', device_addr='..', group_bssid='..', ssid='DIRECT-03-Toyota51807', ... } }
inline bool wfdPersistedGoSsid(const std::string& reply, std::string& ssid) {
    size_t pos = 0;
    while ((pos = reply.find("role='GO'", pos)) != std::string::npos) {
        // " ssid='" with the space: "group_bssid='" also ends in ssid='
        size_t s = reply.find(" ssid='", pos);
        size_t next = reply.find("role='", pos + 9);
        if (s == std::string::npos || (next != std::string::npos && s > next)) { pos += 9; continue; }
        s += 7;
        size_t e = reply.find('\'', s);
        if (e == std::string::npos) return false;
        std::string v = reply.substr(s, e - s), dn;
        if (wfdDeviceNameFromSsid(v, dn)) { ssid = v; return true; }
        pos = e;
    }
    return false;
}

// The child's last line `wfd-own-result: ok=1 ssid=DIRECT-03-Toyota51807` -> ok + ssid. False if there is no such line.
inline bool wfdOwnParseResult(const std::string& out, bool& ok, std::string& ssid) {
    size_t p = out.rfind("wfd-own-result:");
    if (p == std::string::npos) return false;
    std::string line = out.substr(p);
    size_t nl = line.find_first_of("\r\n");
    if (nl != std::string::npos) line = line.substr(0, nl);
    ok = line.find(" ok=1") != std::string::npos;
    ssid.clear();
    size_t s = line.find(" ssid=");
    if (s != std::string::npos) ssid = wfdTrim(line.substr(s + 6));
    return true;
}

// One-shot child: nbt-aad --wfd-own <passphrase> [ssid]. Prints progress lines and the result line; 0 = group on.
int runWfdOwnCommand(int argc, char** argv);

// ---- watch: does the car take the group back? ---------------------------------------------------------------------
// The group's passphrase cannot be read back (wfd_own.hpp top), so the watch looks at what the car must change to put
// its own passphrase back: Wi-Fi Direct switched off, the group set up again (the car's dhcpd on wfd0 restarts with a
// new pid on every WFD_disable/enable - car-proven), a different group name, or a rewritten WFDPass.conf.
// NBT_WFD_WATCH_S: seconds between checks (default 20, 0 = off, 5..600).
inline int wfdWatchSetting(const char* e) {
    if (!e || !*e) return 20;
    int v = atoi(e);
    if (v <= 0) return 0;
    return v < 5 ? 5 : v > 600 ? 600 : v;
}
inline int wfdWatchSec() { return wfdWatchSetting(getenv("NBT_WFD_WATCH_S")); }

struct WfdWatchState {
    int on;                 // isWFDEnable wfd_on: 1, 0, -1 = no answer
    std::string ssid;       // persisted GO group name, empty = none / no answer
    long dhcpdPid;          // the car's dhcpd on wfd0, 0 = not running
    std::string carFile;    // "<path> <mtime> <size>" of the first WFDPass.conf, empty = none
    WfdWatchState() : on(-1), dhcpdPid(0) {}
};

// The status child's line `wfd-own-status: on=1 ssid=DIRECT-03-Toyota51807` -> on (-1 when not answered) + ssid.
inline bool wfdStatusParse(const std::string& out, int& on, std::string& ssid) {
    size_t p = out.rfind("wfd-own-status:");
    if (p == std::string::npos) return false;
    std::string line = out.substr(p);
    size_t nl = line.find_first_of("\r\n");
    if (nl != std::string::npos) line = line.substr(0, nl);
    on = line.find(" on=1") != std::string::npos ? 1 : line.find(" on=0") != std::string::npos ? 0 : -1;
    ssid.clear();
    size_t s = line.find(" ssid=");
    if (s != std::string::npos) ssid = wfdTrim(line.substr(s + 6));
    return true;
}

// `pidin ar` text -> pid of the dhcpd serving wfd0 (the line's leading number), 0 when there is none.
inline long wfdDhcpdPid(const std::string& pidin) {
    size_t pos = 0;
    while (pos < pidin.size()) {
        size_t e = pidin.find('\n', pos);
        std::string l = pidin.substr(pos, e == std::string::npos ? std::string::npos : e - pos);
        pos = e == std::string::npos ? pidin.size() : e + 1;
        if (l.find("dhcpd") == std::string::npos || l.find("wfd0") == std::string::npos) continue;
        size_t i = l.find_first_not_of(" \t");
        if (i == std::string::npos || l[i] < '0' || l[i] > '9') continue;
        return atol(l.c_str() + i);
    }
    return 0;
}

// Empty when `now` still looks like nbt-aad's group as set up at `base`, else why not. Unknowns (no answer) never
// count: a missed status call is not the car taking the group back.
inline std::string wfdWatchVerdict(const WfdWatchState& base, const WfdWatchState& now) {
    if (now.on == 0) return "the car switched Wi-Fi Direct off";
    if (base.dhcpdPid > 0 && now.dhcpdPid == 0) return "the car's dhcpd on wfd0 is gone";
    if (base.dhcpdPid > 0 && now.dhcpdPid > 0 && now.dhcpdPid != base.dhcpdPid) {
        char b[96];
        snprintf(b, sizeof(b), "the group was set up again (dhcpd on wfd0 restarted, pid %ld -> %ld)", base.dhcpdPid,
                 now.dhcpdPid);
        return b;
    }
    if (!base.ssid.empty() && !now.ssid.empty() && now.ssid != base.ssid)
        return "the group name changed ('" + base.ssid + "' -> '" + now.ssid + "')";
    if (base.carFile != now.carFile) return "the car rewrote its WFDPass.conf";
    return "";
}

// ---- the group's name for the phone (every mode) -------------------------------------------------------------------
// The head-unit ladder (alt_wifi.hpp resolveWfdSsid: NBT_WFD_SSID, ifconfig, mwud, the WLAN persistency) first; when it
// has nothing (Y24073J: no ssid line, mwud refuses with errno 261, no persistency), the car's connection manager's
// stored group name (getPersistedGroupList). Only a DIRECT-xx-<name> counts. `source` says which one answered.
inline bool wfdPickSsid(bool ladderOk, const std::string& ladder, const std::string& connMngr, std::string& ssid,
                        std::string& source) {
    std::string dn;
    if (ladderOk && !ladder.empty()) { ssid = ladder; source = "head unit"; return true; }
    if (!connMngr.empty() && wfdDeviceNameFromSsid(connMngr, dn)) {
        ssid = connMngr;
        source = "the car's connection manager";
        return true;
    }
    ssid.clear();
    source.clear();
    return false;
}

// What DIRECT mode is missing, for the "wireless Android Auto is unavailable" line. ownTried: nbt-aad tried to set its
// own passphrase (NBT_WFD_OWN_PASS decided so) and failed.
enum WfdMissing { WFD_MISSING_NOTHING = 0, WFD_MISSING_PASS = 1, WFD_MISSING_SSID = 2, WFD_MISSING_BOTH = 3 };
inline WfdMissing wfdMissing(bool havePass, bool haveSsid) {
    return (WfdMissing)((havePass ? 0 : 1) | (haveSsid ? 0 : 2));
}
inline std::string wfdMissingText(WfdMissing m, bool ownTried) {
    std::string pass = ownTried ? "no usable Wi-Fi Direct passphrase (nbt-aad's own could not be set, see the [WfdOwn] "
                                  "lines, and no usable WFDPass.conf)"
                                : "no usable Wi-Fi Direct passphrase (/var/opt/conn/WFDPass.conf, then "
                                  "/fs/sda1/opt/conn/WFDPass.conf)";
    std::string ssid = "no Wi-Fi Direct group name (not from the head unit's files, mwud or the car's connection manager; "
                       "NBT_WFD_SSID=DIRECT-03-<brand><last5> pins one)";
    switch (m) {
    case WFD_MISSING_PASS: return pass + "; the group name is known";
    case WFD_MISSING_SSID: return "the passphrase is fine, but " + ssid;
    case WFD_MISSING_BOTH: return pass + ", and " + ssid;
    default: return "the Wi-Fi Direct group could not be offered";
    }
}

// Daemon side: the group name from the car's connection manager (the --wfd-own-status child), cached once found; a
// miss is asked again at most every 30 s. Logs each outcome once.
bool wfdSsidFromConnMngr(std::string& ssid);

// One-shot child: nbt-aad --wfd-own-status. Prints `wfd-own-status: on=<0|1|-1> ssid=<persisted GO ssid>`.
int runWfdStatusCommand();
// Daemon side: pidin + WFDPass.conf stat, and with `gcf` also the child above (it loads the car libraries and registers
// a GCF client, so the watch asks it only now and then). False when the child was asked for and could not be run.
bool wfdOwnStatus(WfdWatchState& st, bool gcf);

// Daemon side: our passphrase (NBT_WFD_PASS, else the stored one, else a new one stored in the data dir), then the
// child above. On success `ap` is filled (ssid, key, WPA2) and true is returned. Logs every step.
bool applyWfdOwnPass(ApInfo& ap, std::string& passPath);

} // namespace nbt

#endif // WFD_OWN_HPP
