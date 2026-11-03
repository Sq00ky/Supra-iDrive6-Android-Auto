// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// wfd_probe - read-only diagnostic for the car's Wi-Fi Direct group (wfd0).
//
// Standalone tool (own main, not linked into the daemon). It prints everything the daemon needs to offer the
// DIRECT group to a phone, so one run says where each piece comes from:
//   * `ifconfig wfd0` / `ifconfig -a` verbatim (does the interface print an ssid line at all?)
//   * the decoded WFDPass.conf passphrase and WFDWps.conf PIN (see src/alt_wifi.hpp for the mechanism)
//   * raw replies from mwud on 127.0.0.1:9096 for the commands that carry the group name
//   * the relevant config files and the Wi-Fi persistency mapper (printable strings)
// It changes nothing: no mwud command here starts, stops or reconfigures anything.
//
// Usage:  wfd_probe            (copy it to a local dir on the head unit first: /mnt/umass* is noexec)
//         wfd_probe > /mnt/umass01200t7/wfd_probe.log
#include "../src/alt_wifi.hpp"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

using namespace nbt;

static void oneLine(const char* tag, const std::string& text, size_t cap) {
    std::string flat;
    for (size_t i = 0; i < text.size() && i < cap; ++i) {
        unsigned char c = (unsigned char)text[i];
        flat.push_back(c == '\r' ? ' ' : (c == '\n' ? '|' : (char)c));
    }
    printf("%s %s%s\n", tag, flat.c_str(), text.size() > cap ? " ...(cut)" : "");
}

static void runCmd(const char* tag, const char* cmd, size_t cap) {
    std::string out;
    FILE* p = popen(cmd, "r");
    if (!p) { printf("%s cannot run '%s'\n", tag, cmd); return; }
    char line[512];
    while (fgets(line, sizeof(line), p)) out += line;
    pclose(p);
    std::string trimmed = wfdTrim(out);
    if (trimmed.empty()) { printf("%s '%s' printed nothing\n", tag, cmd); return; }
    oneLine(tag, trimmed, cap);
}

static void printFile(const char* path) {
    std::string text;
    if (!readWholeFile(path, text)) { printf("[file] %s: not readable\n", path); return; }
    oneLine("[file]", std::string(path) + " (" + (text.size() ? "content" : "empty") + "):" + text, 400);
}

static void decodeSecretFile(const char* path, bool pin) {
    std::string text, out, why;
    if (!readWholeFile(path, text)) { printf("[secret] %s: not readable\n", path); return; }
    if (!decodeWfdSecret(text, out, why)) { printf("[secret] %s: %s\n", path, why.c_str()); return; }
    bool ok = pin ? wfdPinOk(out) : wfdPassphraseOk(out);
    printf("[secret] %s: decoded '%s' (%u chars) %s\n", path, out.c_str(), (unsigned)out.size(),
           ok ? "OK" : "(NOT a passphrase/PIN)");
}

static void printStrings(const char* path, const char* tag, size_t minRun) {
    std::string text;
    if (!readWholeFile(path, text)) { printf("%s %s: not readable\n", tag, path); return; }
    std::string cur;
    std::string found;
    for (size_t i = 0; i <= text.size(); ++i) {
        unsigned char c = i < text.size() ? (unsigned char)text[i] : 0;
        if (c >= 0x20 && c < 0x7f) { cur.push_back((char)c); continue; }
        if (cur.size() >= minRun) {
            if (found.size() + cur.size() > 700) { found += " ...(cut)"; break; }
            found += cur + "|";
        }
        cur.clear();
    }
    printf("%s %s: %s\n", tag, path, found.empty() ? "(no printable runs)" : found.c_str());
}

// mwud: try the framing variants once each, and say which half failed (connect vs read) - the car gave no reply
// at all on 2026-11-03, so the next run has to distinguish "9096 closed" from "request shape wrong".
static void mwu(const char* cmd, unsigned flags) {
    std::string body = "module=wifidirect\niface=wfd0\ncmd=";
    body += cmd;
    body += "\n";
    std::string info;
    std::string reply = mwuCommandEx(body, 1200, flags, &info);
    std::string tag = "[mwud] ";
    tag += cmd;
    tag += (flags & 2) ? " (hdr type 1)" : " (hdr type 0)";
    tag += (flags & 1) ? " CRLF:" : " LF:";
    if (reply.empty()) {
        printf("%s %s\n", tag.c_str(), info.c_str());
        return;
    }
    std::string ssid;
    oneLine(tag.c_str(), reply, 900);
    if (parseMwuSsid(reply, ssid)) printf("[mwud] %s: group name '%s'\n", cmd, ssid.c_str());
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    printf("[wfd_probe] build " __DATE__ " " __TIME__ "\n");

    runCmd("[ifconfig]", "ifconfig wfd0", 1200);
    std::string ssid;
    if (resolveWfdSsid("wfd0", ssid)) printf("[ifconfig] resolved group name: '%s'\n", ssid.c_str());
    else printf("[ifconfig] no group name from ifconfig or mwud yet\n");

    printf("---- passphrase / PIN ----\n");
    for (size_t i = 0; i < wfdPassPathCount(); ++i) decodeSecretFile(wfdPassPaths()[i], false);
    for (size_t i = 0; i < wfdWpsPathCount(); ++i) decodeSecretFile(wfdWpsPaths()[i], true);

    printf("---- config files ----\n");
    printFile("/var/opt/conn/wifidir_init.conf");
    printFile("/opt/conn/bin/wifidir_init.conf");
    printFile("/var/opt/conn/dhcpd.wfd0.conf");
    printFile("/var/opt/conn/dhcpd.wfd0.leases");
    runCmd("[ls]", "ls -l /var/opt/conn", 1200);

    printf("---- mwud 127.0.0.1:9096 (read-only commands) ----\n");
    mwu("p2p_get_cfg", 0);
    mwu("p2p_get_cfg", 1);
    mwu("p2p_get_cfg", 2);
    mwu("p2p_get_cfg", 3);
    mwu("list_groups", 0);
    mwu("list_groups", 3);
    mwu("dump_peers", 0);

    printf("---- persistency ----\n");
    printStrings("/var/opt/sys/persistence/early/pers_ConnectivityWlanPersistencyPersMapper", "[persist]", 4);
    printStrings("/var/opt/conn/ConnMngr.rnf", "[connmngr]", 6);
    for (size_t i = 0; i < wfdPersistPathCount(); ++i) {
        std::string text, name;
        if (!readWholeFile(wfdPersistPaths()[i], text)) continue;
        if (wfdDeviceNameFromPersistency(text, name))
            printf("[derive] device name '%s' -> group '%s'\n", name.c_str(), wfdSsidFromDeviceName(name).c_str());
        else
            printf("[derive] %s carries no '<name> CarPlay' run\n", wfdPersistPaths()[i]);
    }

    printf("[wfd_probe] done\n");
    return 0;
}
