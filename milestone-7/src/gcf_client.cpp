// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// GCF client for the factory connection manager - see gcf_client.hpp.
#include "gcf_client.hpp"

#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

namespace nbt {

// ---------------------------------------------------------------- reply parsing
bool gcfField(const std::string& msg, const std::string& key, std::string& value) {
    size_t pos = 0;
    while ((pos = msg.find(key + "=", pos)) != std::string::npos) {
        if (pos > 0) {
            char b = msg[pos - 1];
            if (b != ' ' && b != '{' && b != ',' && b != ';' && b != '[') { pos += key.size(); continue; }
        }
        size_t v = pos + key.size() + 1, e = v;
        if (e < msg.size() && msg[e] == '\'') {
            ++e;
            while (e < msg.size() && msg[e] != '\'') ++e;
            if (e < msg.size()) ++e;
        } else if (e < msg.size() && (msg[e] == '{' || msg[e] == '[')) {
            int depth = 0;
            bool inq = false;
            for (; e < msg.size(); ++e) {
                char c = msg[e];
                if (c == '\'') inq = !inq;
                if (inq) continue;
                if (c == '{' || c == '[') ++depth;
                if (c == '}' || c == ']') { if (--depth == 0) { ++e; break; } }
            }
        } else {
            while (e < msg.size() && msg[e] != ' ' && msg[e] != ',' && msg[e] != ';' && msg[e] != '}' && msg[e] != ']')
                ++e;
        }
        value = msg.substr(v, e - v);
        return true;
    }
    return false;
}

int gcfResult(const std::string& msg) {
    std::string r;
    return gcfField(msg, "result", r) ? atoi(r.c_str()) : -9999;
}

std::string gcfUnquote(const std::string& v) {
    if (v.size() >= 2 && v[0] == '\'' && v[v.size() - 1] == '\'') return v.substr(1, v.size() - 2);
    return v;
}

// ---------------------------------------------------------------- transport (libsac.so)
namespace {
typedef void* (*sss_open_t)(const char*, int);
typedef int (*sss_write_t)(void*, const char*, unsigned);
typedef int (*sss_read_t)(void*, char*, unsigned, unsigned*);
sss_open_t p_open;
sss_write_t p_write;
sss_read_t p_read;
void* g_h;

pthread_mutex_t g_mx = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t g_cv = PTHREAD_COND_INITIALIZER;
std::vector<std::string> g_inbox;
bool g_readerDead;

double nowSec() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec / 1e6;
}

void* readerMain(void*) {
    static char buf[65536];
    for (;;) {
        unsigned len = 0;
        int r = p_read(g_h, buf, sizeof(buf) - 1, &len);
        if (r <= 0) break;
        unsigned n = (unsigned)r < sizeof(buf) - 1 ? (unsigned)r : (unsigned)sizeof(buf) - 1;
        if (len && len < n) n = len;
        std::string msg(buf, n);
        while (!msg.empty() && msg[msg.size() - 1] == '\0') msg.erase(msg.size() - 1);
        pthread_mutex_lock(&g_mx);
        g_inbox.push_back(msg);
        pthread_cond_signal(&g_cv);
        pthread_mutex_unlock(&g_mx);
    }
    pthread_mutex_lock(&g_mx);
    g_readerDead = true;
    pthread_cond_signal(&g_cv);
    pthread_mutex_unlock(&g_mx);
    return NULL;
}

bool nextMsg(double sec, std::string& out) {
    double deadline = nowSec() + sec;
    pthread_mutex_lock(&g_mx);
    while (g_inbox.empty() && !g_readerDead) {
        double left = deadline - nowSec();
        if (left <= 0) break;
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        long ns = ts.tv_nsec + (long)((left - (long)left) * 1e9);
        ts.tv_sec += (time_t)left + ns / 1000000000L;
        ts.tv_nsec = ns % 1000000000L;
        pthread_cond_timedwait(&g_cv, &g_mx, &ts);
    }
    bool got = !g_inbox.empty();
    if (got) { out = g_inbox.front(); g_inbox.erase(g_inbox.begin()); }
    pthread_mutex_unlock(&g_mx);
    return got;
}

bool sendFrame(const std::string& f) {
    int w = p_write(g_h, f.c_str(), (unsigned)f.size());
    return w == (int)f.size();
}
} // namespace

GcfClient::GcfClient() : seq_(1) {}

bool GcfClient::open(const char* name, std::string& why) {
    // Plain names: found through LD_LIBRARY_PATH=/opt/conn/lib (set by whoever started us).
    void* sac = NULL;
    const int modes[2] = {RTLD_NOW | RTLD_GLOBAL, RTLD_LAZY | RTLD_GLOBAL};
    for (int m = 0; m < 2 && !sac; ++m) {
        if (!dlopen("libtools_common.so", modes[m])) { why = std::string("libtools_common.so: ") + dlerror(); continue; }
        dlopen("libparsercreator.so", modes[m]);
        sac = dlopen("libsac.so", modes[m]);
        if (!sac) why = std::string("libsac.so: ") + dlerror();
    }
    if (!sac) return false;
    p_open = (sss_open_t)dlsym(sac, "sss_open");
    p_write = (sss_write_t)dlsym(sac, "sss_write");
    p_read = (sss_read_t)dlsym(sac, "sss_read");
    if (!p_open || !p_write || !p_read) { why = "libsac.so has no sss_open/sss_write/sss_read"; return false; }
    g_h = p_open("/dev/scp_pss", 0);
    if (!g_h) { why = std::string("sss_open(/dev/scp_pss) failed: ") + strerror(errno); return false; }
    char me[64];
    snprintf(me, sizeof(me), "%s_%d", name, (int)getpid());
    me_ = me;
    pthread_t t;
    if (pthread_create(&t, NULL, readerMain, NULL) != 0) { why = "cannot start the reply reader"; return false; }
    pthread_detach(t);
    if (!sendFrame("CTRL REGS " + me_ + " GCF_Version='1.0' INFO='' VERSION='' CONFIG={  };") ||
        !sendFrame("CTRL REGA " + me_ + " ADDR=" + me_ + ";") || !sendFrame("CTRL REGF " + me_ + " ;")) {
        why = "registration write failed";
        return false;
    }
    std::string m;
    while (nextMsg(0.5, m)) {}   // CTRL CNFG GCFROUTER MODE=STANDARD;
    return true;
}

std::string GcfClient::call(const std::string& method, const std::string& params, int timeoutSec) {
    char seq[16];
    snprintf(seq, sizeof(seq), "%u", seq_++);
    const std::string tag = me_ + ":" + seq;
    if (!sendFrame("CALL " + tag + " " + method + (params.empty() ? " ;" : " " + params + ";"))) return "";
    double deadline = nowSec() + timeoutSec;
    std::string m;
    while (nowSec() < deadline) {
        if (!nextMsg(deadline - nowSec(), m)) break;
        bool mine = m.find(tag + " ") != std::string::npos || m.find(tag + ";") != std::string::npos ||
                    (m.compare(0, 4, "EVNT") != 0 && m.find(method) != std::string::npos &&
                     m.find(me_) != std::string::npos);
        if (mine && m.compare(0, 4, "CALL") != 0) return m;
    }
    return "";
}

} // namespace nbt
