// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
//
// nbt-pf: one-time packet-filter setup for Android Auto (see tools/pf_aa.hpp for the why).
//   1. Reads /fs/sda0/etc/pf.conf (the file the car loads at boot as /etc/pf.conf).
//   2. Already passes TCP 5277 on uap0 and wfd0: says so, adds nothing.
//      Otherwise: checks the new file with `pfctl -n` (parse only), keeps the original as pf.conf.orig (first run
//      only) and appends the missing rules at the end.
//   3. With the rules in place, sets NBT_KEEP_PF=1 in /opt/aa/nbt-aad.env, so the daemon stops running `pfctl -d`.
// /fs/sda0 is remounted read-write for the edit when it is read-only, and read-only again afterwards.
// Nothing is loaded into the running filter: the car reads pf.conf at its next start.
//
// Usage: nbt-pf            do it
//        nbt-pf --check    only report, change nothing
//        nbt-pf --pf <file> --env <file>   other paths (tests)

#include "pf_aa.hpp"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

const char* const kPfDefault = "/fs/sda0/etc/pf.conf";
const char* const kPfFallback = "/etc/pf.conf";
const char* const kEnvDefault = "/opt/aa/nbt-aad.env";
#ifndef NBT_PFCTL
#define NBT_PFCTL "/bin/pfctl"   // host smoke test: -DNBT_PFCTL=<fake>
#endif
const char* const kPfctl = NBT_PFCTL;
const char* const kMountPoint = "/fs/sda0";
#ifndef NBT_PF_CHECKFILE
#define NBT_PF_CHECKFILE "/dev/shmem/nbt-pf-check.conf"   // parse-only copy for pfctl -n (RAM)
#endif
const char* const kCheckFile = NBT_PF_CHECKFILE;

bool exists(const char* path) {
    struct stat st;
    return stat(path, &st) == 0;
}

// Whole file into out. False with errno set when it cannot be opened.
bool readFile(const char* path, std::string& out) {
    out.clear();
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
    fclose(f);
    return true;
}

bool writeAll(int fd, const std::string& data) {
    size_t done = 0;
    while (done < data.size()) {
        ssize_t w = write(fd, data.data() + done, data.size() - done);
        if (w < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        done += (size_t)w;
    }
    return true;
}

// New file with data, flushed to disk. False with errno set.
bool writeNewFile(const char* path, const std::string& data, mode_t mode, bool exclusive) {
    int fd = open(path, O_WRONLY | O_CREAT | (exclusive ? O_EXCL : O_TRUNC), mode);
    if (fd < 0) return false;
    bool ok = writeAll(fd, data) && fsync(fd) == 0;
    int e = errno;
    close(fd);
    errno = e;
    return ok;
}

std::string dirOf(const char* path) {
    std::string p = path;
    size_t s = p.rfind('/');
    if (s == std::string::npos) return ".";
    return s == 0 ? "/" : p.substr(0, s);
}

bool readOnly(const std::string& dir) {
    struct statvfs v;
    if (statvfs(dir.c_str(), &v) != 0) return false;
    return (v.f_flag & ST_RDONLY) != 0;
}

// Runs cmd with stderr folded into the captured output; returns its exit status (-1: could not run it).
int run(const std::string& cmd, std::string& out) {
    out.clear();
    FILE* p = popen((cmd + " 2>&1").c_str(), "r");
    if (!p) return -1;
    char buf[512];
    while (fgets(buf, sizeof buf, p)) out += buf;
    int st = pclose(p);
    if (st == -1 || !WIFEXITED(st)) return -1;
    return WEXITSTATUS(st);
}

const char* yesNo(bool b) { return b ? "TCP 5277 allowed" : "TCP 5277 NOT allowed"; }

int usage() {
    printf("usage: nbt-pf [--check] [--pf <pf.conf>] [--env <nbt-aad.env>]\n");
    return 1;
}

} // namespace

int main(int argc, char** argv) {
    bool check = false;
    const char* pf = NULL;
    const char* env = kEnvDefault;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--check")) check = true;
        else if (!strcmp(argv[i], "--pf") && i + 1 < argc) pf = argv[++i];
        else if (!strcmp(argv[i], "--env") && i + 1 < argc) env = argv[++i];
        else return usage();
    }
    if (!pf) pf = exists(kPfDefault) ? kPfDefault : kPfFallback;

    printf("nbt-pf: Android Auto (TCP 5277) in the car's packet filter%s\n", check ? " - check only" : "");

    std::string conf;
    if (!readFile(pf, conf)) {
        printf("ERROR: cannot read %s: %s\n", pf, strerror(errno));
        return 1;
    }
    nbt::pfaa::Coverage have = nbt::pfaa::coverage(conf);
    printf("%s:\n  uap0 (CarPlay hotspot):    %s\n  wfd0 (Wi-Fi Direct group): %s\n", pf, yesNo(have.uap0),
           yesNo(have.wfd0));
    std::string info;
    if (run(std::string(kPfctl) + " -s info", info) >= 0) {
        size_t s = info.find("Status:");
        if (s != std::string::npos) {
            size_t e = info.find('\n', s);
            printf("packet filter now: %s\n", info.substr(s, e == std::string::npos ? std::string::npos : e - s).c_str());
        }
    }

    bool needPf = !have.uap0 || !have.wfd0;
    std::string newConf = nbt::pfaa::withRules(conf, have);

    std::string envText;
    bool envExists = readFile(env, envText);
    if (!envExists && errno != ENOENT) {
        printf("ERROR: cannot read %s: %s\n", env, strerror(errno));
        return 1;
    }
    nbt::pfaa::EnvEdit envEdit = nbt::pfaa::setKeepPf(envText);

    if (!needPf) printf("Already added: pf.conf lets Android Auto through on both networks. Nothing to add.\n");
    if (!envEdit.changed) printf("%s already has NBT_KEEP_PF=1.\n", env);
    if (check) {
        if (needPf) printf("Would append the Android Auto rules to %s.\n", pf);
        if (envEdit.changed) printf("Would set NBT_KEEP_PF=1 in %s.\n", env);
        return 0;
    }
    if (!needPf && !envEdit.changed) return 0;

    // Parse the new pf.conf before touching the real one.
    if (needPf) {
        std::string out;
        if (!writeNewFile(kCheckFile, newConf, 0644, false)) {
            printf("ERROR: cannot write %s: %s. Nothing changed.\n", kCheckFile, strerror(errno));
            return 2;
        }
        int rc = run(std::string(kPfctl) + " -n -f " + kCheckFile, out);
        unlink(kCheckFile);
        if (rc != 0) {
            printf("ERROR: '%s -n -f' rejected the new pf.conf (rc %d):\n%sNothing changed.\n", kPfctl, rc, out.c_str());
            return 2;
        }
        printf("pfctl -n: the new pf.conf parses.\n");
    }

    // Writable for the edit.
    bool remounted = false;
    if ((needPf && readOnly(dirOf(pf))) || (envEdit.changed && readOnly(dirOf(env)))) {
        printf("remounting %s read-write\n", kMountPoint);
        std::string out;
        run(std::string("mount -uw ") + kMountPoint, out);
        remounted = true;
        if ((needPf && readOnly(dirOf(pf))) || (envEdit.changed && readOnly(dirOf(env)))) {
            printf("ERROR: still read-only after 'mount -uw %s'%s%s. Nothing changed.\n", kMountPoint,
                   out.empty() ? "" : ": ", out.c_str());
            run(std::string("mount -ur ") + kMountPoint, out);
            return 3;
        }
    }

    int rc = 0;
    if (needPf) {
        struct stat st;
        mode_t mode = stat(pf, &st) == 0 ? (st.st_mode & 07777) : 0644;
        std::string orig = std::string(pf) + ".orig";
        if (!exists(orig.c_str())) {
            if (writeNewFile(orig.c_str(), conf, mode, true)) printf("original kept as %s\n", orig.c_str());
            else printf("WARNING: could not keep the original as %s: %s\n", orig.c_str(), strerror(errno));
        }
        // Append in place (the same file and link the car reads), like a hand edit.
        int fd = open(pf, O_WRONLY | O_APPEND);
        bool ok = fd >= 0 && writeAll(fd, newConf.substr(conf.size())) && fsync(fd) == 0;
        int e = errno;
        if (fd >= 0) close(fd);
        std::string check2;
        if (!ok) {
            printf("ERROR: appending to %s failed: %s\n", pf, strerror(e));
            rc = 4;
        } else if (!readFile(pf, check2) || check2 != newConf) {
            printf("ERROR: %s does not read back as written; restore it with: cp %s %s\n", pf, orig.c_str(), pf);
            rc = 4;
        } else {
            printf("added to %s:\n%s", pf, newConf.substr(conf.size()).c_str());
        }
    }

    if (rc == 0 && envEdit.changed) {
        std::string tmp = std::string(env) + ".nbt-new";
        struct stat st;
        mode_t mode = envExists && stat(env, &st) == 0 ? (st.st_mode & 07777) : 0644;
        if (!writeNewFile(tmp.c_str(), envEdit.text, mode, false) || rename(tmp.c_str(), env) != 0) {
            printf("ERROR: cannot write %s: %s. Add the line NBT_KEEP_PF=1 to it by hand.\n", env, strerror(errno));
            unlink(tmp.c_str());
            rc = 5;
        } else {
            printf("set NBT_KEEP_PF=1 in %s (setting %d of %d)\n", env, envEdit.position, envEdit.settings);
            if (envEdit.position > nbt::pfaa::kMaxEnvSettings)
                printf("WARNING: only the first %d settings of %s are used: move NBT_KEEP_PF=1 up or drop a line.\n",
                       nbt::pfaa::kMaxEnvSettings, env);
        }
    }

    if (remounted) {
        std::string out;
        int m = run(std::string("mount -ur ") + kMountPoint, out);
        printf("remounted %s read-only%s\n", kMountPoint, m == 0 ? "" : " - FAILED, run: mount -ur /fs/sda0");
    }
    if (rc) return rc;

    if (needPf)
        printf("The car loads pf.conf when it starts: restart the head unit (or load it now: pfctl -f %s -e).\n", pf);
    if (envEdit.changed)
        printf("NBT_KEEP_PF=1 applies from the next Android Auto Start in iDrive: the packet filter stays on.\n");
    if (needPf) printf("Undo: cp %s.orig %s\n", pf, pf);
    return 0;
}
