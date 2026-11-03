// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Host test: Wi-Fi Direct group credentials (NBT_ALT_WIFI=DIRECT).
// The ciphertext vectors are real car values: /var/opt/conn/WFDPass.conf on the J29 read
// 3B0F277431217E050505 and the decoded passphrase is uMs1GNN240.
#include "alt_wifi.hpp"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

static void put(const char* p, const char* body) {
    FILE* f = fopen(p, "w");
    if (!f) { printf("FAIL cannot write %s\n", p); ++fails; return; }
    fputs(body, f);
    fclose(f);
}

int main() {
    // ---- decode: the real car file ------------------------------------------------
    std::string out, why;
    CHECK(nbt::decodeWfdSecret("3B0F277431217E050505", out, why));
    CHECK(out == "uMs1GNN240");
    CHECK(nbt::wfdPassphraseOk(out));
    // 16-hex WPS PIN file, same key
    out.clear();
    CHECK(nbt::decodeWfdSecret("7F70677143590707", out, why));
    CHECK(out == "12345670");

    // ---- decode: whitespace, as the file on the car carries CRLF -------------------
    out.clear();
    CHECK(nbt::decodeWfdSecret("3B0F277431217E050505\r\n", out, why));
    CHECK(out == "uMs1GNN240");
    out.clear();
    CHECK(nbt::decodeWfdSecret("  3B0F277431217E050505 \n\n", out, why));
    CHECK(out == "uMs1GNN240");

    // ---- decode: rejected inputs ---------------------------------------------------
    out.clear();
    CHECK(!nbt::decodeWfdSecret("", out, why) && !why.empty());
    CHECK(!nbt::decodeWfdSecret("3B0F2", out, why));                   // odd length
    CHECK(!nbt::decodeWfdSecret("3B0F277431217E05050G", out, why));    // not hex
    CHECK(!nbt::decodeWfdSecret("zz", out, why));

    // ---- passphrase policy: WPA2 needs 8..63 printable chars ----------------------
    CHECK(nbt::wfdPassphraseOk("uMs1GNN240"));
    CHECK(!nbt::wfdPassphraseOk("short"));
    CHECK(!nbt::wfdPassphraseOk(std::string("a\x01" "cdefghij")));
    CHECK(!nbt::wfdPassphraseOk(std::string(64, 'a')));
    CHECK(nbt::wfdPassphraseOk(std::string(63, 'a')));

    // ---- NBT_ALT_WIFI parsing -----------------------------------------------------
    // default since v1.8.78: BOTH (the hotspot when the car has one, the DIRECT group behind it)
    CHECK(nbt::parseAltWifi(NULL) == nbt::ALT_WIFI_BOTH);
    CHECK(nbt::parseAltWifi("") == nbt::ALT_WIFI_BOTH);
    CHECK(nbt::parseAltWifi("CARPLAY") == nbt::ALT_WIFI_CARPLAY);
    CHECK(nbt::parseAltWifi("carplay") == nbt::ALT_WIFI_CARPLAY);
    CHECK(nbt::parseAltWifi("DIRECT") == nbt::ALT_WIFI_DIRECT);
    CHECK(nbt::parseAltWifi("direct") == nbt::ALT_WIFI_DIRECT);
    CHECK(nbt::parseAltWifi("BOTH") == nbt::ALT_WIFI_BOTH);
    CHECK(nbt::parseAltWifi("both") == nbt::ALT_WIFI_BOTH);
    CHECK(nbt::parseAltWifi("banana") == nbt::ALT_WIFI_BOTH);      // unknown word: the default
    CHECK(strcmp(nbt::altWifiName(nbt::ALT_WIFI_DIRECT), "DIRECT") == 0);
    // which source is offered first: only DIRECT puts the Wi-Fi Direct group ahead of the CarPlay hotspot
    setenv("NBT_ALT_WIFI", "CARPLAY", 1);
    CHECK(nbt::altWifiPrefersCarplay());
    setenv("NBT_ALT_WIFI", "BOTH", 1);
    CHECK(nbt::altWifiPrefersCarplay());
    setenv("NBT_ALT_WIFI", "DIRECT", 1);
    CHECK(!nbt::altWifiPrefersCarplay());
    unsetenv("NBT_ALT_WIFI");
    CHECK(nbt::altWifiPrefersCarplay());

    // ---- SSID from `ifconfig wfd0` ------------------------------------------------
    std::string ssid;
    CHECK(nbt::parseIfconfigSsid("wfd0: flags=8843<UP,BROADCAST,RUNNING,SIMPLEX,MULTICAST> mtu 1500\n"
                                 "        ssid: DIRECT-03-WBA5A0A012345678\n"
                                 "        address: 00:0d:87:12:34:56\n"
                                 "        inet 172.16.222.1 netmask 0xffffff00 broadcast 172.16.222.255\n",
                                 ssid));
    CHECK(ssid == "DIRECT-03-WBA5A0A012345678");
    ssid.clear();
    CHECK(!nbt::parseIfconfigSsid("wfd0: flags=8843<UP>\n        inet 172.16.222.1 netmask 0xffffff00\n", ssid));
    // no comment lines and no trailing whitespace leaking into the value
    ssid.clear();
    CHECK(nbt::parseIfconfigSsid("ssid : DIRECT-03-ABC\n", ssid));
    CHECK(ssid == "DIRECT-03-ABC");

    // ---- SSID from an mwud reply body --------------------------------------------
    ssid.clear();
    CHECK(nbt::parseMwuSsid("status=0\nmodule=wifidirect\nssid=DIRECT-03-TOYO123\n", ssid));
    CHECK(ssid == "DIRECT-03-TOYO123");
    ssid.clear();
    CHECK(nbt::parseMwuSsid("status=0\ngo_ssid=DIRECT-03-XY\n", ssid));
    CHECK(ssid == "DIRECT-03-XY");
    // mwud's own state-dump form (module prefix, colon separator)
    ssid.clear();
    CHECK(nbt::parseMwuSsid("WIFIDIR: go_ssid: DIRECT-03-ABC\n", ssid));
    CHECK(ssid == "DIRECT-03-ABC");
    ssid.clear();
    CHECK(nbt::parseMwuSsid("Current SSID:DIRECT-03-XYZ \n", ssid));
    CHECK(ssid == "DIRECT-03-XYZ");
    ssid.clear();
    CHECK(nbt::parseMwuSsid("status=0\nGO_SSID=DIRECT-03-GO\n", ssid));
    CHECK(ssid == "DIRECT-03-GO");
    ssid.clear();
    CHECK(!nbt::parseMwuSsid("status=0\n", ssid));
    CHECK(!nbt::parseMwuSsid("status=0\nssid=\n", ssid));   // empty value is not a name

    // ---- SSID derivation from Connectivity's Wi-Fi persistency -------------------
    // Car blob (probe on the J29): "Toyota51807 <nul> Toyota51807 CarPlay <nul> <20 hex> <nul>"
    std::string dev;
    CHECK(nbt::wfdDeviceNameFromPersistency(
        std::string("Toyota51807\0Toyota51807 CarPlay\0" "240B6170131A7370505E\0", 47), dev));
    CHECK(dev == "Toyota51807");
    CHECK(nbt::wfdSsidFromDeviceName(dev) == "DIRECT-03-Toyota51807");
    // the group name itself must never be taken from the encrypted secret (20 uppercase hex)
    dev.clear();
    CHECK(!nbt::wfdDeviceNameFromPersistency("240B6170131A7370505E", dev));
    // no CarPlay name in the blob: refuse rather than guess
    CHECK(!nbt::wfdDeviceNameFromPersistency("Toyota51807", dev));
    // spaces inside the name are removed (removeWhiteSpaces in CWlanWiFiDirectCfg::startUp)
    dev.clear();
    CHECK(nbt::wfdDeviceNameFromPersistency("BMW 51709 CarPlay\n", dev));
    CHECK(dev == "BMW51709");
    // CarPlay AP SSID -> device name (the same rule, for cars that do have iap2.cfg)
    dev.clear();
    CHECK(nbt::wfdDeviceNameFromCarplaySsid("Toyota51807 CarPlay", dev) && dev == "Toyota51807");
    dev.clear();
    CHECK(!nbt::wfdDeviceNameFromCarplaySsid("", dev));
    CHECK(!nbt::wfdDeviceNameFromCarplaySsid("SomeOtherNetwork", dev));

    // ---- listener address follows the Wi-Fi source -------------------------------
    // The bug the car showed on 2026-11-03: phone told 172.16.222.1, listener bound to 172.16.224.1.
    unsetenv("NBT_AP_IP");
    unsetenv("NBT_WFD_IP");
    setenv("NBT_ALT_WIFI", "CARPLAY", 1);
    CHECK(nbt::wirelessListenIp() && strcmp(nbt::wirelessListenIp(), "172.16.224.1") == 0);
    setenv("NBT_ALT_WIFI", "DIRECT", 1);
    CHECK(nbt::wirelessListenIp() && strcmp(nbt::wirelessListenIp(), "172.16.222.1") == 0);
    setenv("NBT_WFD_IP", "172.16.222.9", 1);
    CHECK(strcmp(nbt::wirelessListenIp(), "172.16.222.9") == 0);
    unsetenv("NBT_WFD_IP");
    setenv("NBT_ALT_WIFI", "BOTH", 1);
    CHECK(nbt::wirelessListenIp() == NULL);            // both subnets: listen on every interface
    setenv("NBT_AP_IP", "172.16.224.7", 1);            // an explicit address still wins
    CHECK(nbt::wirelessListenIp() && strcmp(nbt::wirelessListenIp(), "172.16.224.7") == 0);
    unsetenv("NBT_AP_IP");

    // accept filter: the peer must sit in the network of the address the connection arrived on
    unsigned local222 = 0xAC10DE01u;                   // 172.16.222.1 (wfd0)
    unsigned local224 = 0xAC10E001u;                   // 172.16.224.1 (uap0)
    CHECK(nbt::wirelessPeerAllowed(0xAC10DE64u, local222));    // 172.16.222.100
    CHECK(!nbt::wirelessPeerAllowed(0xAC10E064u, local222));   // joined the CarPlay AP instead
    CHECK(nbt::wirelessPeerAllowed(0xAC10E064u, local224));
    CHECK(!nbt::wirelessPeerAllowed(0xAC10DE64u, local224));
    CHECK(!nbt::wirelessPeerAllowed(0x0A000001u, local222));   // 10.0.0.1
    CHECK(!nbt::wirelessPeerAllowed(0xAC10DE64u, 0x7f000001u));// 127/8 is never a phone
    CHECK(!nbt::wirelessPeerAllowed(0xAC10DE64u, 0u));         // bound to 0.0.0.0: no network to compare
    // and it works for a car that numbers wfd0 differently, e.g. 10.77.0.1 / 10.77.0.23
    CHECK(nbt::wirelessPeerAllowed(0x0A4D0017u, 0x0A4D0001u));

    // BOTH binds every interface, so the filter uses the cached uap0/wfd0 networks instead of the arrival address
    unsigned nets[2] = { 0xAC10DE00u, 0xAC10E000u };   // 172.16.222.0, 172.16.224.0
    CHECK(nbt::wfdPeerInNets(0xAC10DE64u, nets, 2, 0u));
    CHECK(nbt::wfdPeerInNets(0xAC10E064u, nets, 2, 0u));
    CHECK(!nbt::wfdPeerInNets(0xA030C701u, nets, 2, 0u));          // 160.48.199.1 (Ethernet/vehicle net)
    CHECK(!nbt::wfdPeerInNets(0xAC10DE64u, nets, 0, 0x7f000001u)); // no interface read: arrival rule, loopback

    // interface address parser: this is what `ifconfig wfd0` really prints on the J29
    std::string ifip;
    CHECK(nbt::parseIfconfigIpv4(
        "wfd0: flags=8843<UP,BROADCAST,RUNNING,SIMPLEX,MULTICAST> mtu 1500\n"
        "\taddress: 3a:b8:00:48:90:c3\n"
        "\tmedia: <unknown type> autoselect\n"
        "\tinet 172.16.222.1 netmask 0xffffff00 broadcast 172.16.222.255\n"
        "\tlink 3a:b8:00:48:10:c3\n",
        ifip));
    CHECK(ifip == "172.16.222.1");
    ifip.clear();
    CHECK(!nbt::parseIfconfigIpv4("wfd0: flags=8843<UP>\n\taddress: 3a:b8:00:48:90:c3\n", ifip));
    unsetenv("NBT_ALT_WIFI");                          // unset = today's behaviour = CarPlay

    // ---- loadWfdAp: file -> ApInfo -----------------------------------------------
    const char* pass = "/tmp/nbt_wfd_pass.conf";
    const char* wps  = "/tmp/nbt_wfd_wps.conf";
    const char* junk = "/tmp/nbt_wfd_junk.conf";
    const char* miss = "/tmp/nbt_wfd_missing.conf";
    unlink(miss);
    put(pass, "3B0F277431217E050505\r\n");
    put(wps,  "7F70677143590707\n");
    put(junk, "not-a-passphrase-file\n");

    nbt::ApInfo ap;
    std::string used;
    const char* pPass[] = { miss, pass };
    CHECK(nbt::loadWfdAp(pPass, 2, ap, used));
    CHECK(ap.key == "uMs1GNN240");
    CHECK(ap.security == 8);
    CHECK(ap.apType == 1);
    CHECK(used == pass);
    // the factory path list starts at the live Connectivity file
    CHECK(nbt::wfdPassPathCount() == 2 &&
          strcmp(nbt::wfdPassPaths()[0], "/var/opt/conn/WFDPass.conf") == 0 &&
          strcmp(nbt::wfdPassPaths()[1], "/fs/sda1/opt/conn/WFDPass.conf") == 0);
    CHECK(nbt::wfdWpsPathCount() == 2 && strcmp(nbt::wfdWpsPaths()[0], "/var/opt/conn/WFDWps.conf") == 0);
    // a file that decodes to something that is not a passphrase is refused
    nbt::ApInfo bad;
    const char* pJunk[] = { junk };
    CHECK(!nbt::loadWfdAp(pJunk, 1, bad, used));

    std::string pin;
    const char* pWps[] = { miss, wps };
    CHECK(nbt::loadWfdWpsPin(pWps, 2, pin, used));
    CHECK(pin == "12345670");

    unlink(pass); unlink(wps); unlink(junk);
    printf(fails ? "test_alt_wifi: %d FAILED\n" : "test_alt_wifi: all passed\n", fails);
    return fails ? 1 : 0;
}
