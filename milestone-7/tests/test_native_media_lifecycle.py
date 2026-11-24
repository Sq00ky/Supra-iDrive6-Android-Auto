# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
"""Run main.cpp's real media callbacks with fake audio/cluster consumers, without a car."""
from pathlib import Path
import argparse
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "src/main.cpp").read_text()
start = source.index("static const char* const kMediaFile =")
end = source.index("// One press: a media key", start)
block = source[start:end].replace('"/dev/shmem/nbt-aad-media"', '"media.record"')
start = source.index("static void onAudioState(")
end = source.index("\nstatic double g_sessionStartS", start)
audio = source[start:end]
start = source.index("    g_session.stop();", source.index("// Cleanup & graceful teardown"))
end = source.index('    printf("[Main] [+%.2f s] session stopped', start)
shutdown = source[start:end]
assert "g_mainWake = 0;\n        expireMedia();" in source

harness = r'''
#include "audio/native_media_record.hpp"
#include "aa_session.hpp"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fstream>
#include <iterator>
#include <vector>
static uint32_t clockMs;
static uint32_t monoMs();
static void locked();
struct Nav {
    std::string text;
    void setNowPlaying(const std::string& s) { locked(); text = s; }
} g_kombiNav;
struct Tracks {
    std::string text;
    bool playing;
    void update(const std::string& s, bool p) { locked(); text = s; playing = p; }
} g_kombiMedia;
struct Browser {
    bool active;
    bool mediaListActive() { return active; }
} browser;
static Browser* g_browserAudio = &browser;
struct Audio {
    void onStreamState(uint8_t ch, bool started, uint32_t, uint32_t) {
        if (ch == 9) browser.active = started;
    }
} backend;
static Audio* g_audioBackend = &backend;
static nbt::AASession g_session;

BLOCK
AUDIO

static void stopDaemonSession() {
SHUTDOWN
}

static unsigned disconnects;
static void disconnected(void*) { ++disconnects; resetMedia("session ended"); }

static void locked() {
    int r = pthread_mutex_trylock(&g_mediaMutex);
    if (!r) pthread_mutex_unlock(&g_mediaMutex);
    assert(r == EBUSY);
}
static uint32_t monoMs() { locked(); return clockMs; }
static std::string record() {
    std::ifstream in("media.record");
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}
static nbt::NativeMediaSnapshot snapshot(uint32_t now) {
    nbt::NativeMediaSnapshot out;
    std::string r = record();
    assert(nbt::parseNativeMediaRecord(r.data(), r.size(), now, out));
    return out;
}
static void text(std::vector<uint8_t>& b, unsigned field, const char* s) {
    b.push_back((uint8_t)((field << 3) | 2)); b.push_back((uint8_t)strlen(s));
    b.insert(b.end(), s, s + strlen(s));
}
static void send(uint16_t id, const std::vector<uint8_t>& b, uint32_t now) {
    clockMs = now; onMediaStatus(id, b.empty() ? NULL : &b[0], b.size(), NULL);
}
static void status(unsigned state, const char* app, uint32_t now) {
    std::vector<uint8_t> b; b.push_back(8); b.push_back((uint8_t)state); text(b, 2, app);
    send(0x8001, b, now);
}
static void metadata(const char* title, const char* artist, uint32_t now) {
    std::vector<uint8_t> b; text(b, 1, title); text(b, 2, artist); send(0x8003, b, now);
}
static void tick(uint32_t now) { clockMs = now; expireMedia(); }
static void cleared(uint32_t now) {
    assert(!snapshot(now).active);
    assert(g_kombiNav.text.empty() && g_kombiMedia.text.empty() && !g_kombiMedia.playing);
    assert(record().find("song=\n") != std::string::npos && !g_mediaDirty);
}
int main() {
    signal(SIGPIPE, SIG_IGN);
    (void)g_mediaKeysToPhone; (void)g_wheelMediaKeys;
    resetMedia("new session"); cleared(900);
    browser.active = true;
    status(2, "Player A", 1000);
    metadata("Трек A", "Автор A", 1010);
    assert(g_mediaDirty && !snapshot(1010).active);
    tick(1250);
    assert(snapshot(1250).active && !strcmp(snapshot(1250).artist, "Автор A"));
    status(2, "Player A", 1300);
    tick(1500);
    assert(snapshot(1500).active && snapshot(1500).updatedMs == 1300);

    // The actual stream callback immediately withdraws native eligibility; no media key is involved.
    onAudioState(9, false, 0, 0, NULL);
    assert(!snapshot(1500).active);
    onAudioState(9, true, 48000, 2, NULL);
    assert(snapshot(1500).active);
    status(3, "Player A", 1510);
    assert(!snapshot(1510).active); // pause must not wait for the 200ms throttle
    status(2, "Player B", 1600);
    assert(!snapshot(1600).active && record().find("Автор A") == std::string::npos);
    metadata("Трек B", "Автор B", 1610);
    tick(1850);
    assert(snapshot(1850).active && !strcmp(snapshot(1850).song, "Трек B"));
    std::vector<uint8_t> malformed(1, 0xFF);
    send(0x8001, malformed, 12000);
    assert(g_media.updatedMs == 1600);
    tick(1600 + nbt::kMediaStaleMs);
    cleared(1600 + nbt::kMediaStaleMs);

    // A heartbeat arriving before the main tick cannot resurrect pre-gap metadata.
    status(2, "Player C", 30000);
    metadata("Old track", "Old artist", 30010);
    tick(30250);
    assert(snapshot(30250).active);
    status(2, "Player C", 30000 + nbt::kMediaStaleMs + 1);
    assert(!snapshot(45001).active && g_kombiMedia.text.empty());
    metadata("Pending", "Artist", 45002);
    assert(g_mediaDirty);
    resetMedia("session ended"); cleared(45003);
    tick(46000); cleared(46000);

    // Stop while the phone socket remains open: AASession deliberately skips its disconnect callback.
    g_session.setDisconnectCallback(&disconnected, NULL);
    for (unsigned cycle = 0; cycle != 2; ++cycle) {
        int sockets[2];
        assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
        assert(g_session.start(sockets[0]));
        unsigned char version[12]; size_t received = 0;
        while (received != sizeof(version)) {
            const ssize_t n = read(sockets[1], version + received, sizeof(version) - received);
            assert(n > 0); received += (size_t)n;
        }
        assert(g_session.isActive());
        browser.active = true;
        status(2, "Player", 50000 + cycle * 1000);
        metadata("Current track", "Current artist", 50010 + cycle * 1000);
        tick(50250 + cycle * 1000);
        assert(snapshot(clockMs).active);
        stopDaemonSession();
        assert(!g_session.isActive() && disconnects == 0);
        cleared(clockMs); // No TTL advance and no synthetic native=0 write.
        assert(record().find("artist=\n") != std::string::npos);
        stopDaemonSession(); cleared(clockMs); // Idempotent cleanup.
        close(sockets[1]);
    }
    puts("PASS native media lifecycle: heartbeat, pending flush, stream stop, pause, player change, expiry, reset and real session shutdown/restart");
}
'''.replace("BLOCK", block).replace("AUDIO", audio).replace("SHUTDOWN", shutdown)

compiler = shutil.which("g++") or shutil.which("clang++")
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--emit-qnx", type=Path, help="Emit an isolated QNX shutdown fixture, without compiling or running it")
args = parser.parse_args()
if args.emit_qnx:
    fixture = harness[:harness.index("int main() {")].replace('"media.record"', '"/dev/shmem/nbt-aad-media"')
    fixture += r'''
#include <time.h>
static uint32_t liveMs() {
    struct timespec ts; assert(clock_gettime(CLOCK_MONOTONIC, &ts) == 0);
    return (uint32_t)((uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}
int main() {
    const char* stop = "/dev/shmem/nbt-aad-stop-fixture";
    signal(SIGPIPE, SIG_IGN); setvbuf(stdout, NULL, _IONBF, 0);
    assert(access(stop, F_OK) != 0); // The runner must remove its previous trigger before launch.
    int sockets[2]; assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
    g_session.setDisconnectCallback(&disconnected, NULL);
    assert(g_session.start(sockets[0]));
    unsigned char version[12]; size_t received = 0;
    while (received != sizeof(version)) {
        const ssize_t n = read(sockets[1], version + received, sizeof(version) - received);
        assert(n > 0); received += (size_t)n;
    }
    clockMs = liveMs(); resetMedia("fixture session started");
    browser.active = true;
    status(2, "Native Audio fixture", liveMs());
    metadata("Песня Проверка", "Исполнитель", liveMs());
    usleep(250000); clockMs = liveMs(); expireMedia();
    assert(snapshot(liveMs()).active && g_session.isActive());
    printf("READY pid=%d\n", (int)getpid());
    const uint32_t started = liveMs();
    while (access(stop, F_OK) != 0) {
        if ((uint32_t)(liveMs() - started) > 90000) { stopDaemonSession(); close(sockets[1]); return 2; }
        status(2, "Native Audio fixture", liveMs());
        usleep(100000);
    }
    const uint32_t atStop = liveMs();
    stopDaemonSession();
    assert(!g_session.isActive() && disconnects == 0);
    cleared(liveMs());
    assert(record().find("artist=\n") != std::string::npos);
    close(sockets[1]);
    printf("STOPPED elapsed_ms=%u disconnects=%u\n", (unsigned)(liveMs() - atStop), disconnects);
    return 0;
}
'''
    args.emit_qnx.write_text(fixture)
    raise SystemExit(0)
assert compiler, "A POSIX C++ compiler is required"
with tempfile.TemporaryDirectory(prefix="native-media-lifecycle-") as temp:
    work = Path(temp)
    cpp, exe = work / "lifecycle.cpp", work / "lifecycle"
    cpp.write_text(harness)
    subprocess.run([compiler, "-std=gnu++0x", "-Wall", "-pthread", "-I" + str(ROOT / "src"),
                    "-I" + str(ROOT / "include"), str(cpp),
                    *[str(ROOT / "src" / name) for name in (
                        "aa_session.cpp", "aa_cryptor.cpp", "audio/audio_acks.cpp", "crash_handler.cpp", "sdr_builder.cpp")],
                    "-lssl", "-lcrypto", "-lm", "-o", str(exe)], check=True)
    subprocess.run([str(exe)], cwd=work, check=True)
