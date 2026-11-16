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

#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <string>

namespace nbt {

// NBT_WFD_OWN_PASS: unset/1 = on (DIRECT mode sets nbt-aad's own passphrase), 0/off/no = off (WFDPass.conf as before).
inline bool wfdOwnPassSetting(const char* env) {
    if (!env || !*env) return true;
    return !(strcmp(env, "0") == 0 || strcasecmp(env, "off") == 0 || strcasecmp(env, "no") == 0 ||
             strcasecmp(env, "false") == 0);
}
inline bool wfdOwnPassWanted() { return wfdOwnPassSetting(getenv("NBT_WFD_OWN_PASS")); }

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

// Daemon side: our passphrase (NBT_WFD_PASS, else the stored one, else a new one stored in the data dir), then the
// child above. On success `ap` is filled (ssid, key, WPA2) and true is returned. Logs every step.
bool applyWfdOwnPass(ApInfo& ap, std::string& passPath);

} // namespace nbt

#endif // WFD_OWN_HPP
