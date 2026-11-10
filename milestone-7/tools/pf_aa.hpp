// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef NBT_TOOLS_PF_AA_HPP
#define NBT_TOOLS_PF_AA_HPP

// pf_aa (tools/pf_aa.cpp): let Android Auto through the car's packet filter once, instead of the daemon switching the
// whole filter off with `pfctl -d` at every start. The stock /fs/sda0/etc/pf.conf (loaded at boot by start_net.sh,
// `pfctl -f /etc/pf.conf -e`) blocks everything and opens only CarPlay's ports on the hotspot (uap0) and Miracast's on
// the Wi-Fi Direct group (wfd0). The phone's Android Auto link is one TCP connection to 5277 on either, so four lines
// appended at the end are enough (car-verified on the Supra, 2026-11-05: appended by hand, rebooted, AA connected).
// Appending is valid there: the file ends in filter anchors, and `block all` is not `quick`.
//
// Pure text logic, no I/O: host test tests/test_pf_aa.cpp.

#include <errno.h>
#include <string>
#include <string.h>

namespace nbt {
namespace pfaa {

static const char* const kComment =
    "# Android Auto wireless (NBT-AAD): TCP 5277, both directions, on the CarPlay AP (uap0) and the Wi-Fi Direct group (wfd0)\n";
static const char* const kUapRules =
    "pass quick on uap0 proto tcp from 172.16.224/24 to 172.16.224.1 port 5277 keep state\n"
    "pass quick on uap0 proto tcp from 172.16.224.1 to 172.16.224/24 port 5277 keep state\n";
static const char* const kWfdRules =
    "pass quick on wfd0 proto tcp from 172.16.222/24 to 172.16.222.1 port 5277 keep state\n"
    "pass quick on wfd0 proto tcp from 172.16.222.1 to 172.16.222/24 port 5277 keep state\n";
static const char* const kEnvKey = "NBT_KEEP_PF";
static const int kMaxEnvSettings = 15;   // the iDrive app passes only the first 15 settings of nbt-aad.env

// fsync()'s answer for a file just written. A file system with nothing to flush may say so with an error instead of 0:
// QNX's /dev/shmem (RAM) answers ENOSYS - car, 2026-11-06: "cannot write /dev/shmem/pf_aa-check.conf: Function not
// implemented" although the whole file was there - and POSIX allows EINVAL for files that cannot be synchronised.
// Those count as written; files that matter (pf.conf, pf.bak) are read back and compared anyway.
inline bool syncedOk(int rc, int err) {
    if (rc == 0 || err == ENOSYS || err == EINVAL) return true;
#ifdef ENOTSUP
    if (err == ENOTSUP) return true;
#endif
    return false;
}

inline bool isWordChar(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

// tok occurs in s as a whole word: "5277" in "port {5000,5277}" but not in "port 52770".
inline bool hasToken(const std::string& s, const char* tok) {
    size_t n = strlen(tok), pos = 0;
    while ((pos = s.find(tok, pos)) != std::string::npos) {
        bool before = pos == 0 || !isWordChar(s[pos - 1]);
        bool after = pos + n >= s.size() || !isWordChar(s[pos + n]);
        if (before && after) return true;
        pos += 1;
    }
    return false;
}

struct Coverage {
    bool uap0;   // a pass rule for 5277 that applies on the CarPlay hotspot
    bool wfd0;   // ... on the Wi-Fi Direct group
};

// Which interfaces pf.conf already passes 5277 on. A pass rule naming 5277 and neither interface counts for both.
// Comments (from '#') are ignored, so the comment line alone never counts.
inline Coverage coverage(const std::string& conf) {
    Coverage c = { false, false };
    size_t start = 0;
    while (start < conf.size()) {
        size_t end = conf.find('\n', start);
        if (end == std::string::npos) end = conf.size();
        std::string line = conf.substr(start, end - start);
        size_t hash = line.find('#');
        if (hash != std::string::npos) line.erase(hash);
        if (hasToken(line, "pass") && hasToken(line, "5277")) {
            bool u = hasToken(line, "uap0"), w = hasToken(line, "wfd0");
            if (u) c.uap0 = true;
            if (w) c.wfd0 = true;
            if (!u && !w && !hasToken(line, "on")) c.uap0 = c.wfd0 = true;
        }
        start = end + 1;
    }
    return c;
}

// conf without the Android Auto rules: every pass rule for 5277 and every comment line naming NBT-AAD dropped. On a car
// that got them from pf_aa or by hand this is the stock pf.conf again; it is what /opt/aa/pf.bak holds.
inline std::string stripAaRules(const std::string& conf) {
    std::string out;
    size_t start = 0;
    while (start < conf.size()) {
        size_t end = conf.find('\n', start);
        bool nl = end != std::string::npos;
        if (!nl) end = conf.size();
        std::string line = conf.substr(start, end - start);
        std::string rule = line;
        size_t hash = rule.find('#');
        bool ours = hash != std::string::npos && line.find("NBT-AAD") != std::string::npos &&
                    line.find_first_not_of(" \t") == hash;
        if (hash != std::string::npos) rule.erase(hash);
        if (hasToken(rule, "pass") && hasToken(rule, "5277")) ours = true;
        if (!ours) {
            out += line;
            if (nl) out += '\n';
        }
        start = end + 1;
    }
    return out;
}

// conf with the missing rules appended at the end (unchanged when nothing is missing).
inline std::string withRules(const std::string& conf, const Coverage& have) {
    if (have.uap0 && have.wfd0) return conf;
    std::string out = conf;
    if (!out.empty() && out[out.size() - 1] != '\n') out += '\n';
    out += kComment;
    if (!have.uap0) out += kUapRules;
    if (!have.wfd0) out += kWfdRules;
    return out;
}

struct EnvEdit {
    std::string text;   // the new nbt-aad.env
    bool changed;       // text differs from the input
    int position;       // 1-based position of NBT_KEEP_PF among the file's settings
    int settings;       // settings in the new file
};

// nbt-aad.env with NBT_KEEP_PF=1: an existing NBT_KEEP_PF line with another value is rewritten, none = appended.
// Settings are the non-blank lines that do not start with '#' (the app's rule).
inline EnvEdit setKeepPf(const std::string& env) {
    EnvEdit r;
    r.changed = false;
    r.position = 0;
    r.settings = 0;
    const std::string key = std::string(kEnvKey) + "=";
    size_t start = 0;
    while (start < env.size()) {
        size_t end = env.find('\n', start);
        bool nl = end != std::string::npos;
        if (!nl) end = env.size();
        std::string line = env.substr(start, end - start);
        size_t i = line.find_first_not_of(" \t\r");
        if (i != std::string::npos && line[i] != '#') {
            ++r.settings;
            if (line.compare(i, key.size(), key) == 0) {
                std::string v = line.substr(i + key.size());
                size_t e = v.find_last_not_of(" \t\r");
                v = e == std::string::npos ? std::string() : v.substr(0, e + 1);
                if (!r.position) r.position = r.settings;
                if (v != "1") {
                    line = key + "1";
                    r.changed = true;
                }
            }
        }
        r.text += line;
        if (nl) r.text += '\n';
        start = end + 1;
    }
    if (!r.position) {
        if (!r.text.empty() && r.text[r.text.size() - 1] != '\n') r.text += '\n';
        r.text += key + "1\n";
        r.position = ++r.settings;
        r.changed = true;
    }
    return r;
}

// nbt-aad.env without any NBT_KEEP_PF setting (pf_aa --uninstall: with the stock pf.conf back, the daemon has to switch
// the filter off again). Comment lines stay. removed: how many setting lines were dropped.
inline std::string dropKeepPf(const std::string& env, int* removed) {
    const std::string key = std::string(kEnvKey) + "=";
    std::string out;
    int n = 0;
    size_t start = 0;
    while (start < env.size()) {
        size_t end = env.find('\n', start);
        bool nl = end != std::string::npos;
        if (!nl) end = env.size();
        std::string line = env.substr(start, end - start);
        size_t i = line.find_first_not_of(" \t\r");
        if (i != std::string::npos && line[i] != '#' && line.compare(i, key.size(), key) == 0) {
            ++n;
        } else {
            out += line;
            if (nl) out += '\n';
        }
        start = end + 1;
    }
    if (removed) *removed = n;
    return out;
}

// `pfctl -s info` output -> 1 "Status: Enabled", 0 "Status: Disabled", -1 neither found.
inline int pfStatus(const std::string& info) {
    size_t s = info.find("Status:");
    if (s == std::string::npos) return -1;
    size_t e = info.find('\n', s);
    std::string line = info.substr(s, e == std::string::npos ? std::string::npos : e - s);
    if (line.find("Enabled") != std::string::npos) return 1;
    if (line.find("Disabled") != std::string::npos) return 0;
    return -1;
}

// tools/pf_aa.cpp: the command line (--install | --check | --uninstall [--pf/--env/--backup <file>]); pf_aa's main()
// and nbt-aad's --pf-* options both call it. Returns the exit status.
int run(int argc, char** argv);

} // namespace pfaa
} // namespace nbt

#endif // NBT_TOOLS_PF_AA_HPP
