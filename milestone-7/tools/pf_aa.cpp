// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
//
// pf_aa: let Android Auto through the car's packet filter once, instead of the daemon switching the whole filter off
// with `pfctl -d` at every start (see tools/pf_aa.hpp for the why).
//
//   pf_aa --install     /fs/sda0/etc/pf.conf (the car's /etc/pf.conf) backed up to /opt/aa/pf.bak, the Android Auto
//                       rules appended, NBT_KEEP_PF=1 added to /opt/aa/nbt-aad.env. The new file is parsed with
//                       `pfctl -n` before and after writing; if it does not parse, pf.conf is restored from pf.bak.
//   pf_aa --check       reports only: rules present, pf.conf parses, NBT_KEEP_PF=1 set.
//   pf_aa --uninstall   pf.conf restored from /opt/aa/pf.bak, NBT_KEEP_PF removed (the daemon then runs pfctl -d again).
//                       (--restore is the same.)
//   --pf <file> --env <file> --backup <file>: other paths (tests).
// /fs/sda0 is remounted read-write for the edit when it is read-only, and read-only again afterwards. Nothing is loaded
// into the running filter: the car reads pf.conf at its next start.

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
const char* const kBackupDefault = "/opt/aa/pf.bak";
#ifndef NBT_PFCTL
#define NBT_PFCTL "/bin/pfctl"   // host smoke test: -DNBT_PFCTL=<fake>
#endif
const char* const kPfctl = NBT_PFCTL;
#define PF_AA_VERSION "1.1"   // 1.1: /dev/shmem check copy (fsync ENOSYS) no longer fails the install
const char* const kMountPoint = "/fs/sda0";
#ifndef NBT_PF_CHECKFILE
#define NBT_PF_CHECKFILE "/dev/shmem/pf_aa-check.conf"   // parse-only copy for pfctl -n (RAM)
#endif
const char* const kCheckFile = NBT_PF_CHECKFILE;

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

// Writes data, flushed to disk. flags: O_TRUNC (whole file, in place: the same file and links, which is what the car
// reads) or O_APPEND (data added at the end). False with errno set.
bool writeFile(const char* path, const std::string& data, int flags, mode_t mode) {
    int fd = open(path, O_WRONLY | O_CREAT | flags, mode);
    if (fd < 0) return false;
    bool ok = writeAll(fd, data);
    int e = errno;
    if (ok) {
        int r = fsync(fd);
        e = errno;
        ok = nbt::pfaa::syncedOk(r, r == 0 ? 0 : e);   // /dev/shmem: ENOSYS, nothing to flush
    }
    close(fd);
    errno = e;
    return ok;
}

mode_t modeOf(const char* path, mode_t dflt) {
    struct stat st;
    return stat(path, &st) == 0 ? (st.st_mode & 07777) : dflt;
}

bool readsBack(const char* path, const std::string& want) {
    std::string got;
    return readFile(path, got) && got == want;
}

std::string dirOf(const char* path) {
    std::string p = path;
    size_t s = p.rfind('/');
    if (s == std::string::npos) return ".";
    return s == 0 ? "/" : p.substr(0, s);
}

bool readOnly(const char* path) {
    struct statvfs v;
    if (statvfs(dirOf(path).c_str(), &v) != 0) return false;
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

// pfctl -n (parse only, nothing loaded) on a file. 0 = it parses; out = pfctl's messages.
int parses(const char* path, std::string& out) { return run(std::string(kPfctl) + " -n -f " + path, out); }

// Same for text that is not on disk yet: through a RAM copy, removed again whatever happens. < 0: pfctl did not run.
int parsesText(const std::string& text, std::string& out) {
    if (!writeFile(kCheckFile, text, O_TRUNC, 0644)) {
        out = std::string("cannot write ") + kCheckFile + ": " + strerror(errno) + "\n";
        unlink(kCheckFile);
        return -2;
    }
    int rc = parses(kCheckFile, out);
    unlink(kCheckFile);
    return rc;
}

// Read-write for the edit and read-only again at the end - only when it was read-only.
struct Mount {
    bool remounted;
    Mount() : remounted(false) {}
    // The files about to be written (NULL entries skipped). False: still read-only.
    bool writable(const char* a, const char* b, const char* c) {
        printf("checking filesystem writability\n");
        const char* p[3] = { a, b, c };
        bool ro = false;
        for (int i = 0; i < 3; ++i) if (p[i] && readOnly(p[i])) ro = true;
        if (!ro) return true;
        std::string out;
        printf("  %s is read-only: remounting it read-write\n", kMountPoint);
        run(std::string("mount -uw ") + kMountPoint, out);
        remounted = true;
        for (int i = 0; i < 3; ++i) {
            if (p[i] && readOnly(p[i])) {
                printf("ERROR: %s is still read-only after 'mount -uw %s'%s%s\n", dirOf(p[i]).c_str(), kMountPoint,
                       out.empty() ? "" : ": ", out.c_str());
                return false;
            }
        }
        return true;
    }
    ~Mount() {
        if (!remounted) return;
        std::string out;
        int m = run(std::string("mount -ur ") + kMountPoint, out);
        printf("%s back to read-only%s\n", kMountPoint, m == 0 ? "" : " - FAILED, run: mount -ur /fs/sda0");
    }
};

const char* allowed(bool b) { return b ? "TCP 5277 allowed" : "TCP 5277 NOT allowed"; }

void printCoverage(const nbt::pfaa::Coverage& c) {
    printf("  uap0 (CarPlay hotspot):    %s\n  wfd0 (Wi-Fi Direct group): %s\n", allowed(c.uap0), allowed(c.wfd0));
}

int usage() {
    printf("usage: pf_aa --install | --check | --uninstall\n"
           "  --install    back up pf.conf to /opt/aa/pf.bak, add the Android Auto rules, set NBT_KEEP_PF=1\n"
           "  --check      report only\n"
           "  --uninstall  restore pf.conf from /opt/aa/pf.bak, remove NBT_KEEP_PF (also: --restore)\n");
    return 1;
}

// NBT_KEEP_PF=1 in the env file. 0 = done or already there.
int setEnv(const char* env) {
    std::string text;
    bool existed = readFile(env, text);
    nbt::pfaa::EnvEdit e = nbt::pfaa::setKeepPf(text);
    if (!e.changed) {
        printf("%s already has NBT_KEEP_PF=1\n", env);
        return 0;
    }
    printf("adding NBT_KEEP_PF=1 to %s\n", env);
    std::string tmp = std::string(env) + ".pf_aa-new";
    if (!writeFile(tmp.c_str(), e.text, O_TRUNC, existed ? modeOf(env, 0644) : 0644) || rename(tmp.c_str(), env) != 0) {
        printf("ERROR: cannot write %s: %s. Add the line NBT_KEEP_PF=1 to it by hand.\n", env, strerror(errno));
        unlink(tmp.c_str());
        return 5;
    }
    if (e.position > nbt::pfaa::kMaxEnvSettings)
        printf("WARNING: it is setting %d, but only the first %d settings of %s are used: move it up or drop a line.\n",
               e.position, nbt::pfaa::kMaxEnvSettings, env);
    return 0;
}

int install(const char* pf, const char* env, const char* backup) {
    std::string conf, out;
    if (!readFile(pf, conf)) {
        printf("ERROR: cannot read %s: %s\n", pf, strerror(errno));
        return 1;
    }
    std::string envText;
    readFile(env, envText);
    nbt::pfaa::Coverage have = nbt::pfaa::coverage(conf);
    bool needPf = !have.uap0 || !have.wfd0;
    bool needEnv = nbt::pfaa::setKeepPf(envText).changed;
    struct stat st;
    bool needBackup = needPf || stat(backup, &st) != 0;   // rules added by hand: still keep the stock file for --uninstall
    // The backup is the file without any Android Auto rules (some may have been added by hand): stock.
    std::string stock = nbt::pfaa::stripAaRules(conf);

    Mount m;
    if (!m.writable(needPf ? pf : NULL, needBackup ? backup : NULL, needEnv ? env : NULL)) {
        printf("Nothing changed.\n");
        return 3;
    }

    printf("checking to make sure rules haven't been installed\n");
    printCoverage(have);
    if (!needPf) {
        printf("rules are already installed in %s: nothing to add\n", pf);
        if (needBackup) {
            printf("backing up to %s (the stock file: %s without the AA rules)\n", backup, pf);
            if (!writeFile(backup, stock, O_TRUNC, 0644) || !readsBack(backup, stock))
                printf("WARNING: cannot write %s: %s. --uninstall needs it.\n", backup, strerror(errno));
        }
        int rc = setEnv(env);
        if (rc == 0 && needEnv) printf("NBT_KEEP_PF=1 applies from the next Android Auto Start in iDrive\n");
        return rc;
    }

    // The new file has to parse before anything is written.
    std::string newConf = nbt::pfaa::withRules(conf, have);
    int rc = parsesText(newConf, out);
    if (rc < 0 || rc == 126 || rc == 127) {   // pfctl did not run (sh: 126 not executable, 127 not found)
        printf("ERROR: could not run the '%s -n' check of pf.conf with the rules added:\n%sNothing changed.\n", kPfctl,
               out.empty() ? "(pfctl did not start)\n" : out.c_str());
        return 2;
    }
    if (rc != 0) {
        printf("ERROR: '%s -n' rejects pf.conf with the rules added (rc %d):\n%sNothing changed.\n", kPfctl, rc,
               out.c_str());
        return 2;
    }

    printf("backing up to %s\n", backup);
    if (!writeFile(backup, stock, O_TRUNC, 0644) || !readsBack(backup, stock)) {
        printf("ERROR: cannot write %s: %s. Nothing else changed.\n", backup, strerror(errno));
        return 4;
    }

    printf("writing firewall rules\n");
    mode_t mode = modeOf(pf, 0644);
    out.clear();
    const char* why = NULL;
    if (!writeFile(pf, newConf.substr(conf.size()), O_APPEND, mode)) why = strerror(errno);
    else if (!readsBack(pf, newConf)) why = "it does not read back as written";
    else if (parses(pf, out) != 0) why = "pfctl -n rejects the written file";
    if (why) {
        printf("ERROR: %s: %s\n%s", pf, why, out.c_str());
        printf("restoring %s to how it was before this run\n", pf);
        if (writeFile(pf, conf, O_TRUNC, mode) && readsBack(pf, conf))
            printf("%s is back as it was. NBT_KEEP_PF not set.\n", pf);
        else
            printf("ERROR: restoring %s failed (%s): cp %s %s\n", pf, strerror(errno), backup, pf);
        return 6;
    }
    printf("%s", newConf.substr(conf.size()).c_str());

    rc = setEnv(env);
    if (rc) return rc;
    printf("pf.conf modified, restart pf to take effect (reboot the head unit, or: pfctl -f %s -e)\n", pf);
    return 0;
}

int check(const char* pf, const char* env, const char* backup) {
    std::string conf, envText, bak, out;
    if (!readFile(pf, conf)) {
        printf("ERROR: cannot read %s: %s\n", pf, strerror(errno));
        return 1;
    }
    nbt::pfaa::Coverage have = nbt::pfaa::coverage(conf);
    printf("%s:\n", pf);
    printCoverage(have);
    int bad = 0;
    int rc = parses(pf, out);
    if (rc != 0) {
        printf("ERROR: '%s -n -f %s' rejects it (rc %d):\n%s", kPfctl, pf, rc, out.c_str());
        ++bad;
    }
    if (!have.uap0 || !have.wfd0) {
        printf("rules are missing: pf_aa --install\n");
        ++bad;
    }
    readFile(env, envText);
    if (nbt::pfaa::setKeepPf(envText).changed) {
        printf("NBT_KEEP_PF=1 is not in %s: the daemon still switches the packet filter off\n", env);
        ++bad;
    }
    if (!readFile(backup, bak)) printf("note: no backup at %s\n", backup);
    if (run(std::string(kPfctl) + " -s info", out) >= 0) {
        size_t s = out.find("Status:");
        if (s != std::string::npos) {
            size_t e = out.find('\n', s);
            printf("packet filter now: %s\n", out.substr(s, e == std::string::npos ? std::string::npos : e - s).c_str());
        }
    }
    if (bad) return 7;
    printf("rules look good\n");
    return 0;
}

int uninstall(const char* pf, const char* env, const char* backup) {
    std::string stock, conf, envText, out;
    if (!readFile(pf, conf)) {
        printf("ERROR: cannot read %s: %s\n", pf, strerror(errno));
        return 1;
    }
    bool envExists = readFile(env, envText);
    int removed = 0;
    std::string newEnv = nbt::pfaa::dropKeepPf(envText, &removed);
    bool needEnv = envExists && removed > 0;

    Mount m;
    if (!m.writable(pf, needEnv ? env : NULL, NULL)) {
        printf("Nothing changed.\n");
        return 3;
    }

    printf("checking %s to make sure it doesn't have AA rules in it\n", backup);
    if (!readFile(backup, stock)) {
        printf("ERROR: no backup: %s: %s. Nothing changed.\n", backup, strerror(errno));
        return 1;
    }
    nbt::pfaa::Coverage b = nbt::pfaa::coverage(stock);
    if (b.uap0 || b.wfd0) {
        printf("ERROR: %s has Android Auto rules, so it is not the stock file. Nothing changed.\n", backup);
        return 2;
    }
    int rc = parses(backup, out);
    if (rc != 0) {
        printf("ERROR: '%s -n' rejects %s (rc %d):\n%sNothing changed.\n", kPfctl, backup, rc, out.c_str());
        return 2;
    }

    printf("checking %s\n", pf);
    nbt::pfaa::Coverage have = nbt::pfaa::coverage(conf);
    bool needPf = have.uap0 || have.wfd0;
    if (needPf) {
        printf("has AA rules\n");
        printf("restoring %s from %s\n", pf, backup);
        if (!writeFile(pf, stock, O_TRUNC, modeOf(pf, 0644)) || !readsBack(pf, stock)) {
            printf("ERROR: writing %s failed (%s); run: cp %s %s\n", pf, strerror(errno), backup, pf);
            return 4;
        }
    } else {
        printf("no AA rules: %s left as it is\n", pf);
    }

    if (needEnv) {
        printf("removing NBT_KEEP_PF from %s\n", env);
        std::string tmp = std::string(env) + ".pf_aa-new";
        if (!writeFile(tmp.c_str(), newEnv, O_TRUNC, modeOf(env, 0644)) || rename(tmp.c_str(), env) != 0) {
            printf("ERROR: cannot write %s: %s. Remove the NBT_KEEP_PF line by hand.\n", env, strerror(errno));
            unlink(tmp.c_str());
            return 5;
        }
        printf("the daemon switches the packet filter off again from its next Start\n");
    }
    if (needPf)
        printf("pf.conf restored, restart pf to take effect (reboot the head unit, or: pfctl -f %s)\n", pf);
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    enum { NONE, INSTALL, CHECK, UNINSTALL } mode = NONE;
    const char* pf = NULL;
    const char* env = kEnvDefault;
    const char* backup = kBackupDefault;
    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        if (!strcmp(a, "--install") && mode == NONE) mode = INSTALL;
        else if (!strcmp(a, "--check") && mode == NONE) mode = CHECK;
        else if ((!strcmp(a, "--uninstall") || !strcmp(a, "--restore")) && mode == NONE) mode = UNINSTALL;
        else if (!strcmp(a, "--pf") && i + 1 < argc) pf = argv[++i];
        else if (!strcmp(a, "--env") && i + 1 < argc) env = argv[++i];
        else if (!strcmp(a, "--backup") && i + 1 < argc) backup = argv[++i];
        else return usage();
    }
    if (mode == NONE) return usage();
    if (!pf) {
        struct stat st;
        pf = stat(kPfDefault, &st) == 0 ? kPfDefault : kPfFallback;
    }
    printf("pf_aa " PF_AA_VERSION ": Android Auto (TCP 5277) in the car's packet filter, %s\n", pf);
    if (mode == INSTALL) return install(pf, env, backup);
    if (mode == CHECK) return check(pf, env, backup);
    return uninstall(pf, env, backup);
}
