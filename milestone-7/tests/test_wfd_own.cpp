// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Host test: nbt-aad's own Wi-Fi Direct passphrase (wfd_own.hpp) and the GCF reply parsing it relies on.
// Reply texts are verbatim from the J29 (2026-11-16, tools/aa_ap), MAC addresses shortened.
#include "wfd_own.hpp"
#include "gcf_client.hpp"

#include <stdio.h>
#include <string.h>

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

static void testWatch() {
    // setting
    CHECK(nbt::wfdWatchSetting(NULL) == 20 && nbt::wfdWatchSetting("") == 20);
    CHECK(nbt::wfdWatchSetting("0") == 0 && nbt::wfdWatchSetting("-3") == 0);
    CHECK(nbt::wfdWatchSetting("2") == 5 && nbt::wfdWatchSetting("45") == 45 && nbt::wfdWatchSetting("9999") == 600);
    // status line
    int on = 9;
    std::string ssid;
    CHECK(nbt::wfdStatusParse("[WfdOwn] x\nwfd-own-status: on=1 ssid=DIRECT-03-Toyota51807\n", on, ssid));
    CHECK(on == 1 && ssid == "DIRECT-03-Toyota51807");
    CHECK(nbt::wfdStatusParse("wfd-own-status: on=0 ssid=\n", on, ssid) && on == 0 && ssid.empty());
    CHECK(nbt::wfdStatusParse("wfd-own-status: on=-1 ssid=\n", on, ssid) && on == -1);
    CHECK(!nbt::wfdStatusParse("nothing here\n", on, ssid));
    // pidin ar (QNX shape: right-aligned pid, then the command line)
    const char* pidin =
        "     pid Arguments\n"
        "       1 procnto-instr\n"
        "  540689 /bin/dhcpd -cf /var/opt/conn/dhcpd.uap0.conf -lf /tmp/dhcpd.uap0.leases uap0\n"
        " 1298457 /bin/dhcpd -cf /var/opt/conn/dhcpd.wfd0.conf -lf /tmp/dhcpd.wfd0.leases wfd0\n";
    CHECK(nbt::wfdDhcpdPid(pidin) == 1298457);
    CHECK(nbt::wfdDhcpdPid("     pid Arguments\n  540689 /bin/dhcpd uap0\n") == 0);
    CHECK(nbt::wfdDhcpdPid("") == 0);
    // verdicts
    nbt::WfdWatchState b;
    b.on = 1; b.ssid = "DIRECT-03-Toyota51807"; b.dhcpdPid = 1298457; b.carFile = "/var/opt/conn/WFDPass.conf 1700000000 20";
    nbt::WfdWatchState n = b;
    CHECK(nbt::wfdWatchVerdict(b, n).empty());
    n.on = -1; n.ssid = ""; CHECK(nbt::wfdWatchVerdict(b, n).empty());             // no answer is not a takeover
    n = b; n.on = 0; CHECK(nbt::wfdWatchVerdict(b, n).find("off") != std::string::npos);
    n = b; n.dhcpdPid = 1400001; CHECK(nbt::wfdWatchVerdict(b, n).find("set up again") != std::string::npos);
    n = b; n.dhcpdPid = 0; CHECK(nbt::wfdWatchVerdict(b, n).find("gone") != std::string::npos);
    n = b; n.ssid = "DIRECT-03-BMW12345"; CHECK(nbt::wfdWatchVerdict(b, n).find("name changed") != std::string::npos);
    n = b; n.carFile = "/var/opt/conn/WFDPass.conf 1700009999 20";
    CHECK(nbt::wfdWatchVerdict(b, n).find("WFDPass.conf") != std::string::npos);
    nbt::WfdWatchState noDhcp = b; noDhcp.dhcpdPid = 0; n = noDhcp;                  // baseline without dhcpd seen
    CHECK(nbt::wfdWatchVerdict(noDhcp, n).empty());
    printf("watch: ok\n");
}

static void testPickSsid() {
    std::string s, from;
    // Y24073J: WFDPass.conf fine, ladder empty (no ssid line, mwud errno 261, no persistency) -> connection manager
    CHECK(nbt::wfdPickSsid(false, "", "DIRECT-03-BMW12345", s, from));
    CHECK(s == "DIRECT-03-BMW12345" && from == "the car's connection manager");
    // the ladder (incl. an NBT_WFD_SSID pin) wins when it answers
    CHECK(nbt::wfdPickSsid(true, "DIRECT-03-Toyota51807", "DIRECT-03-Other99999", s, from));
    CHECK(s == "DIRECT-03-Toyota51807" && from == "head unit");
    // nothing usable
    CHECK(!nbt::wfdPickSsid(false, "", "", s, from) && s.empty() && from.empty());
    CHECK(!nbt::wfdPickSsid(false, "", "MyHomeWifi", s, from));       // not DIRECT-xx-<name>
    CHECK(!nbt::wfdPickSsid(true, "", "", s, from));                  // ladderOk with an empty name is not a name
    // what is missing
    CHECK(nbt::wfdMissing(true, true) == nbt::WFD_MISSING_NOTHING);
    CHECK(nbt::wfdMissing(true, false) == nbt::WFD_MISSING_SSID);
    CHECK(nbt::wfdMissing(false, true) == nbt::WFD_MISSING_PASS);
    CHECK(nbt::wfdMissing(false, false) == nbt::WFD_MISSING_BOTH);
    std::string t = nbt::wfdMissingText(nbt::WFD_MISSING_SSID, false);
    CHECK(t.find("passphrase is fine") != std::string::npos && t.find("group name") != std::string::npos);
    CHECK(t.find("own could not be set") == std::string::npos);
    t = nbt::wfdMissingText(nbt::WFD_MISSING_PASS, true);
    CHECK(t.find("own could not be set") != std::string::npos);
    t = nbt::wfdMissingText(nbt::WFD_MISSING_PASS, false);
    CHECK(t.find("WFDPass.conf") != std::string::npos && t.find("own could not be set") == std::string::npos);
    t = nbt::wfdMissingText(nbt::WFD_MISSING_BOTH, false);
    CHECK(t.find("passphrase") != std::string::npos && t.find("group name") != std::string::npos);
    printf("pick ssid: ok\n");
}

int main() {
    testWatch();
    testPickSsid();
    // ---- setting + decision ------------------------------------------------------------
    CHECK(nbt::wfdOwnPassSetting(NULL) == nbt::WFD_OWN_AUTO);
    CHECK(nbt::wfdOwnPassSetting("") == nbt::WFD_OWN_AUTO);
    CHECK(nbt::wfdOwnPassSetting("AUTO") == nbt::WFD_OWN_AUTO);
    CHECK(nbt::wfdOwnPassSetting("1") == nbt::WFD_OWN_ALWAYS);
    CHECK(nbt::wfdOwnPassSetting("yes") == nbt::WFD_OWN_ALWAYS);
    CHECK(nbt::wfdOwnPassSetting("0") == nbt::WFD_OWN_NEVER);
    CHECK(nbt::wfdOwnPassSetting("off") == nbt::WFD_OWN_NEVER);
    CHECK(nbt::wfdOwnPassSetting("NO") == nbt::WFD_OWN_NEVER);
    CHECK(nbt::wfdOwnPassSetting("false") == nbt::WFD_OWN_NEVER);
    // auto: only a car without a usable WFDPass.conf gets ours
    CHECK(!nbt::wfdOwnPassDecide(nbt::WFD_OWN_AUTO, true));
    CHECK(nbt::wfdOwnPassDecide(nbt::WFD_OWN_AUTO, false));
    CHECK(nbt::wfdOwnPassDecide(nbt::WFD_OWN_ALWAYS, true));
    CHECK(nbt::wfdOwnPassDecide(nbt::WFD_OWN_ALWAYS, false));
    CHECK(!nbt::wfdOwnPassDecide(nbt::WFD_OWN_NEVER, true));
    CHECK(!nbt::wfdOwnPassDecide(nbt::WFD_OWN_NEVER, false));

    // ---- generated passphrase: 16 chars from the typeable alphabet, usable --------------
    unsigned char raw[16];
    for (int i = 0; i < 16; ++i) raw[i] = (unsigned char)(i * 37 + 11);
    std::string p = nbt::wfdOwnPassFromBytes(raw, sizeof(raw));
    CHECK(p.size() == 16);
    CHECK(p.find_first_not_of(nbt::wfdOwnPassAlphabet()) == std::string::npos);
    CHECK(nbt::wfdOwnPassUsable(p));
    CHECK(p == nbt::wfdOwnPassFromBytes(raw, sizeof(raw)));          // deterministic for the same bytes
    unsigned char zero[16] = {0};
    CHECK(nbt::wfdOwnPassUsable(nbt::wfdOwnPassFromBytes(zero, 16)));
    CHECK(nbt::wfdOwnPassFromBytes(NULL, 0).size() == 16);

    // ---- usable: WPA2 length and nothing that breaks a '...' GCF value -----------------
    CHECK(nbt::wfdOwnPassUsable("SuperComplexPass1"));              // the one the car accepted
    CHECK(nbt::wfdOwnPassUsable("SuperComplexPass!"));              // '!' was fine on uap0 too
    CHECK(!nbt::wfdOwnPassUsable("short"));
    CHECK(!nbt::wfdOwnPassUsable("has'quote1"));
    CHECK(!nbt::wfdOwnPassUsable("semi;colon1"));
    CHECK(!nbt::wfdOwnPassUsable("brace{abc}1"));
    CHECK(!nbt::wfdOwnPassUsable("back\\slash1"));
    CHECK(!nbt::wfdOwnPassUsable(std::string(64, 'a')));

    // ---- stored file text ----------------------------------------------------------------
    std::string got;
    CHECK(nbt::wfdOwnPassFromText("abcDEF23456789xy\n", got) && got == "abcDEF23456789xy");
    CHECK(nbt::wfdOwnPassFromText("  abcDEF23456789xy\r\n", got) && got == "abcDEF23456789xy");
    CHECK(nbt::wfdOwnPassFromText("abcDEF23456789xy\nsecond line\n", got) && got == "abcDEF23456789xy");
    CHECK(!nbt::wfdOwnPassFromText("", got));
    CHECK(!nbt::wfdOwnPassFromText("1234567\n", got));

    // ---- group name <-> device_name -----------------------------------------------------
    std::string dn;
    CHECK(nbt::wfdDeviceNameFromSsid("DIRECT-03-Toyota51807", dn) && dn == "Toyota51807");
    CHECK(nbt::wfdDeviceNameFromSsid("DIRECT-03-BMW12345", dn) && dn == "BMW12345");
    CHECK(!nbt::wfdDeviceNameFromSsid("DIRECT-", dn));
    CHECK(!nbt::wfdDeviceNameFromSsid("DIRECT-03-", dn));
    CHECK(!nbt::wfdDeviceNameFromSsid("Toyota51807 CarPlay", dn));
    CHECK(!nbt::wfdDeviceNameFromSsid("DIRECT-03-a'b", dn));

    // ---- getPersistedGroupList: the J29's reply (group_bssid must not be taken for ssid) ----------
    const char* pg =
        "RESP aa_ap_696432:1 Wlan_ConnMngr_WFD_getPersistedGroupList interfaceName='wfd0' persistedGroup_List={ "
        "{ index=0, role='GO', device_addr='3A:B8:00:48:10:C3', group_bssid='3A:B8:00:48:90:C3', "
        "ssid='DIRECT-03-Toyota51807', count=2, persistedDevice_List={ { device_Name='', mac_Addr='1A:B6' }, "
        "{ device_Name='', mac_Addr='E6:76' } } } } result=0;";
    std::string ssid;
    CHECK(nbt::wfdPersistedGoSsid(pg, ssid) && ssid == "DIRECT-03-Toyota51807");
    CHECK(!nbt::wfdPersistedGoSsid("RESP x:1 Wlan_ConnMngr_WFD_getPersistedGroupList interfaceName='wfd0' "
                                   "persistedGroup_List={  } result=0;", ssid));
    // a client-role entry first, then the GO
    CHECK(nbt::wfdPersistedGoSsid("persistedGroup_List={ { index=0, role='GC', group_bssid='x', ssid='DIRECT-xy-Phone' }, "
                                  "{ index=1, role='GO', group_bssid='y', ssid='DIRECT-03-BMW12345' } } result=0;", ssid) &&
          ssid == "DIRECT-03-BMW12345");

    // ---- child's result line --------------------------------------------------------------
    bool ok = false;
    CHECK(nbt::wfdOwnParseResult("[WfdOwn] ...\nwfd-own-result: ok=1 ssid=DIRECT-03-Toyota51807\n", ok, ssid));
    CHECK(ok && ssid == "DIRECT-03-Toyota51807");
    CHECK(nbt::wfdOwnParseResult("wfd-own-result: ok=0 ssid=\n", ok, ssid) && !ok && ssid.empty());
    CHECK(!nbt::wfdOwnParseResult("[WfdOwn] cannot talk to the car\n", ok, ssid));

    // ---- GCF reply fields (car replies) ------------------------------------------------------
    std::string v;
    const char* isOn = "RESP aa_ap_942191:1 Wlan_ConnMngr_WFD_isWFDEnable interfaceName='wfd0' wfd_on=1 result=0;";
    CHECK(nbt::gcfField(isOn, "wfd_on", v) && v == "1");
    CHECK(nbt::gcfResult(isOn) == 0);
    CHECK(nbt::gcfResult("RESP x:1 Wlan_ConnMngr_WFD_enable interfaceName='wfd0' device_name='NBTAAD' "
                         "ssid='DIRECT-03-NBTAAD' number_of_sec_devices=2 secondary_device_type={ 6, 4 } "
                         "passphrase='x' result=2;") == 2);
    CHECK(nbt::gcfResult("RESP x:1 Wlan_ConnMngr_WFD_disable interfaceName='wfd0' result=7;") == 7);
    CHECK(nbt::gcfResult("") == -9999);
    const char* keys = "RESP x:2 NWS_IEEE80211_getKeylist interface='uap0' keylist={ 'abc' } keyindex=0 result=0;";
    CHECK(nbt::gcfField(keys, "keylist", v) && v == "{ 'abc' }");
    CHECK(nbt::gcfField(keys, "keyindex", v) && v == "0");
    CHECK(nbt::gcfField("x ssid='Toyota51807 CarPlay' result=0;", "ssid", v) && nbt::gcfUnquote(v) == "Toyota51807 CarPlay");
    // "group_bssid=" must not satisfy a lookup of "bssid"/"ssid" (key boundary)
    CHECK(nbt::gcfField("{ group_bssid='aa', ssid='DIRECT-03-X' }", "ssid", v) && v == "'DIRECT-03-X'");

    if (fails) { printf("%d check(s) failed\n", fails); return 1; }
    printf("test_wfd_own: all checks passed\n");
    return 0;
}
