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
    assert(snapshot(now).cursor == nbt::kNativeMediaCursorNone && snapshot(now).cursorMs == 0);
    assert(g_kombiNav.text.empty() && g_kombiMedia.text.empty() && !g_kombiMedia.playing);
    assert(record().find("song=\n") != std::string::npos && !g_mediaDirty);
}
int main() {
    signal(SIGPIPE, SIG_IGN);
    (void)g_mediaKeysToPhone; g_wheelMediaKeys = true;
    resetMedia("new session"); cleared(900);
    browser.active = true;
    status(2, "Player A", 1000);
    metadata("Трек A", "Автор A", 1010);
    assert(g_mediaDirty && !snapshot(1010).active);
    tick(1250);
    assert(snapshot(1250).active && !strcmp(snapshot(1250).artist, "Автор A"));
    assert(nativeMediaWheelCursor() == nbt::kNativeMediaCursorNext);
    status(2, "Player A", 1300);
    tick(1500);
    assert(snapshot(1500).active && snapshot(1500).updatedMs == 1300);
    mirrorNativeMediaWheel(-1, true);
    assert(nbt::nativeMediaCursor(snapshot(1500), 1500) == nbt::kNativeMediaCursorNext);

    // The actual stream callback immediately withdraws native eligibility; no media key is involved.
    g_mediaDirty = true;
    onAudioState(9, false, 0, 0, NULL);
    assert(!snapshot(1500).active && snapshot(1500).cursor == nbt::kNativeMediaCursorNone);
    onAudioState(9, true, 48000, 2, NULL);
    assert(snapshot(1500).active && snapshot(1500).cursor == nbt::kNativeMediaCursorNone);
    mirrorNativeMediaWheel(1, true);
    status(3, "Player A", 1510);
    assert(!snapshot(1510).active && snapshot(1510).cursor == nbt::kNativeMediaCursorNone); // immediate pause clear
    status(2, "Player B", 1600);
    assert(!snapshot(1600).active && record().find("Автор A") == std::string::npos);
    metadata("Трек B", "Автор B", 1610);
    tick(1850);
    assert(snapshot(1850).active && !strcmp(snapshot(1850).song, "Трек B"));
    mirrorNativeMediaWheel(-1, true);
    std::vector<uint8_t> malformed(1, 0xFF);
    send(0x8001, malformed, 12000);
    assert(g_media.updatedMs == 1600);
    tick(1600 + nbt::kMediaStaleMs);
    cleared(1600 + nbt::kMediaStaleMs);

    // Closing the popup does not forget the button; heartbeats are not wheel activity.
    status(2, "Selection A", 20000); metadata("Track", "Artist", 20010); tick(20250);
    mirrorNativeMediaWheel(1, true);
    assert(nbt::nativeMediaCursor(snapshot(20250), 20250) == nbt::kNativeMediaCursorPrev);
    status(2, "Selection A", 23000); tick(23250);
    assert(snapshot(23250).cursorMs == 20250);
    // Saved Yandex Music trace: Stopped -> new metadata -> Playing, with no media stream close.
    status(1, "Selection A", 23500); tick(23750);
    assert(snapshot(23750).active && snapshot(23750).cursor == nbt::kNativeMediaCursorPrev);
    metadata("Next track", "Next artist", 23760); tick(24000);
    assert(snapshot(24000).active && !strcmp(snapshot(24000).song, "Next track"));
    assert(snapshot(24000).cursor == nbt::kNativeMediaCursorPrev);
    status(2, "Selection A", 24251); tick(24500);
    assert(snapshot(24500).active && nbt::nativeMediaCursor(snapshot(24500), 24500) == nbt::kNativeMediaCursorPrev);
    clockMs = 24501; mirrorNativeMediaWheel(0, false);
    assert(snapshot(24501).cursor == nbt::kNativeMediaCursorPrev && snapshot(24501).cursorMs == 24501);
    assert(nativeMediaWheelCursor() == nbt::kNativeMediaCursorPrev);
    // Other captured ordering: Stopped -> Playing -> metadata. No native placeholder in either ordering.
    status(1, "Selection A", 24510); tick(24750);
    assert(snapshot(24750).active && snapshot(24750).cursor == nbt::kNativeMediaCursorPrev);
    status(2, "Selection A", 24760); metadata("Third track", "Third artist", 24770); tick(25000);
    assert(snapshot(25000).active && !strcmp(snapshot(25000).song, "Third track"));
    status(1, "Selection A", 25001);
    onAudioState(9, false, 0, 0, NULL); // A real stream close still clears even with Stopped playback status.
    assert(!snapshot(25001).active && snapshot(25001).cursor == nbt::kNativeMediaCursorNone);
    onAudioState(9, true, 48000, 2, NULL);
    assert(snapshot(25001).active);
    clockMs = 25002; mirrorNativeMediaWheel(-1, true);
    status(2, "Selection B", 25010);
    assert(snapshot(25010).cursor == nbt::kNativeMediaCursorNone && !snapshot(25010).active);
    metadata("Track B", "Artist B", 25020); tick(25250); mirrorNativeMediaWheel(-1, true);
    resetMedia("selection session ended"); cleared(25250);
    for (unsigned dirty = 0; dirty != 2; ++dirty) {
        const uint32_t now = 27000 + dirty * 500;
        status(2, "Cleanup", now); metadata("Track", "Artist", now + 10); tick(now + 250);
        mirrorNativeMediaWheel(-1, true);
        g_mediaDirty = dirty != 0;
        resetMedia("clean or dirty selection reset"); cleared(now + 250);
    }

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
        mirrorNativeMediaWheel(-1, true);
        stopDaemonSession();
        assert(!g_session.isActive() && disconnects == 0);
        cleared(clockMs); // No TTL advance and no synthetic native=0 write.
        assert(record().find("artist=\n") != std::string::npos);
        stopDaemonSession(); cleared(clockMs); // Idempotent cleanup.
        close(sockets[1]);
    }
    puts("PASS native media lifecycle: captured Stopped/Playing/metadata orderings, heartbeat, stream close, pause, player change, expiry and real session shutdown/restart");
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

# Real MOST decoder -> real main callback -> real sendMediaKey; only the phone sink and non-input consumers are fakes.
key_start = source.index("static void sendMediaKey(")
key_end = source.index("// The gesture camera's", key_start)
knob_start = source.index("static void onKnobEvent(")
knob_end = source.index("\n#define NBT_AAD_BUILD", knob_start)
input_harness = r'''
#include "audio/native_media_record.hpp"
#include "car/ccs_input.hpp"
#include "cluster/ric_media.hpp"
#include "most_fixture.hpp"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fstream>
#include <iterator>
#include <utility>
#include <vector>
static std::vector<std::pair<uint32_t, bool> > phoneKeys;
namespace nbt {
struct AASession {
    void postKeyEvent(uint32_t key, bool down) { phoneKeys.push_back(std::make_pair(key, down)); }
    void postRelativeEvent(uint32_t, int) { assert(false); }
};
}
static nbt::AASession g_session;
static bool g_sessionActive = true, g_inputToPhone = false, g_useProfileWheelPolicy = false;
static uint32_t clockMs, g_backDownMs, g_keyToPhoneMs, g_knobInputMs;
static int g_backState, g_hideReq;
static uint32_t monoMs() { return clockMs; }
static uint32_t backHoldMs() { return 0; }
static bool menuOpensIdrive() { return false; }
static nbt::riclist::WheelTurns g_wheelTurns;
struct Nav { void setNowPlaying(const std::string&) {} } g_kombiNav;
struct Tracks {
    void update(const std::string&, bool) {}
    unsigned directKeys = 0;
    void directTrackKeySeen() { ++directKeys; }
    int wheelPress(std::string* why, int) { if (why) *why = "fake MOST cluster"; return nbt::riclist::kPressNone; }
    bool kombiSeen() { return false; }
} g_kombiMedia;
struct Browser { bool mediaListActive() { return true; } } browser;
static Browser* g_browserAudio = &browser;
BLOCK
KEY
KNOB
static nbt::NativeMediaSnapshot snapshot(uint32_t now) {
    std::ifstream in("media.record");
    const std::string record((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    nbt::NativeMediaSnapshot out;
    assert(nbt::parseNativeMediaRecord(record.data(), record.size(), now, out));
    return out;
}
static void frame(uint16_t fkt, uint8_t value, uint32_t now) {
    clockMs = now;
    nbt::most::Msg m;
    m.txType = 0; m.varTag = 2; m.varVal = 0; m.dst = 0x205D; m.src = 0x1001;
    m.fblock = 0x15; m.inst = 2; m.fkt = fkt; m.opType = nbt::most::kOpStatus; m.hdrTag = 0;
    m.data.assign(1, value);
    const std::vector<uint8_t> wire = nbt::most::buildMessage(m);
    fake_dsi::data(nbt::most::kInfoMessage, &wire[0], (uint32_t)wire.size());
}
static void pair(size_t index, uint32_t key) {
    assert(phoneKeys.size() == index + 2);
    assert(phoneKeys[index] == std::make_pair(key, true));
    assert(phoneKeys[index + 1] == std::make_pair(key, false));
}
int main() {
    setenv("NBT_KOMBI_TRACKS", "1", 1); setenv("NBT_MFL_LOG", "0", 1);
    unsetenv("NBT_KOMBI_TRACKS_PRESS"); unsetenv("NBT_MFL_NEXT"); unsetenv("NBT_MFL_PREV");
    unsetenv("NBT_MFL_PLAYPAUSE");
    fake_dsi::reset();
    nbt::most::Client client; nbt::car::CcsInput input;
    assert(input.attach(client, nbt::car::CcsConfig(), &onKnobEvent, &g_session));
    most_fixture::startClient(client);
    g_wheelMediaKeys = g_mediaKeysToPhone = true;
    g_media.state = 2; g_media.song = "Track"; g_media.artist = "Artist";
    g_media.haveStatus = g_media.haveMetadata = true; g_media.updatedMs = 1000; g_mediaNative = true;
    const uint8_t playing[] = {8, 2}; clockMs = 1000;
    onMediaStatus(0x8001, playing, sizeof(playing), NULL);

    frame(0xC30, 0x7F, 1000); frame(0xC31, 0, 1010); frame(0xC31, 1, 1020);
    frame(0xC31, 1, 1030); frame(0xC31, 0, 1040);
    pair(0, nbt::AAK_MEDIA_NEXT); // Initial display and the first press agree on the lower button.
    assert(snapshot(1040).cursor == nbt::kNativeMediaCursorNext);
    frame(0xC30, 0x7E, 1200); frame(0xC30, 0x7E, 1210);
    assert(snapshot(1210).cursorMs == 1200); // A repeated raw status is not another event.
    frame(0xC30, 0x7F, 1250);
    pair(0, nbt::AAK_MEDIA_NEXT);
    assert(snapshot(1250).cursor == nbt::kNativeMediaCursorNext);
    frame(0xC31, 1, 1300); pair(2, nbt::AAK_MEDIA_NEXT);
    for (uint32_t now = 1310; now != 1350; now += 10) frame(0xC31, 1, now);
    frame(0xC31, 0, 1350); pair(2, nbt::AAK_MEDIA_NEXT);
    frame(0xC30, 0x80, 1400); frame(0xC30, 0x7F, 1450);
    frame(0xC31, 1, 1500); frame(0xC31, 0, 1550); pair(4, nbt::AAK_MEDIA_PREVIOUS);

    // Passive UI polling and drawing preparation cannot become another media-command source.
    for (unsigned i = 0; i != 100; ++i) {
        const nbt::NativeMediaSnapshot current = snapshot(1600);
        assert(nbt::nativeMediaCursor(current, 1600) == nbt::kNativeMediaCursorPrev);
        uint16_t text[26]; nbt::nativeMediaText(current.song, text); nbt::nativeMediaText(current.artist, text);
    }
    clockMs = 1600; expireMedia(); pair(4, nbt::AAK_MEDIA_PREVIOUS);
    frame(0xC30, 0x7E, 2000); frame(0xC30, 0x7F, 2050);
    assert(nbt::nativeMediaCursor(snapshot(6050), 6050) == nbt::kNativeMediaCursorNext);
    assert(nbt::nativeMediaCursor(snapshot(6051), 6051) == nbt::kNativeMediaCursorNext);
    frame(0xC31, 1, 6051); frame(0xC31, 0, 6070);
    pair(6, nbt::AAK_MEDIA_NEXT);
    assert(snapshot(6070).cursor == nbt::kNativeMediaCursorNext);
    frame(0xC30, 0x80, 8000); frame(0xC30, 0x7F, 8050);
    assert(snapshot(13000).cursor == nbt::kNativeMediaCursorPrev); // Rest retains the last known direction.
    g_media.position = 57; // Restart-current versus previous-track semantics belong to the player.
    frame(0xC31, 1, 13050); frame(0xC31, 0, 13060); pair(8, nbt::AAK_MEDIA_PREVIOUS);

    const uint16_t skip[] = {0xC07, 0x511};
    uint32_t now = 13100;
    for (unsigned i = 0; i != 2; ++i) for (uint8_t value = 1; value != 3; ++value) {
        const size_t count = phoneKeys.size();
        frame(skip[i], value, now++); frame(skip[i], value, now++); frame(skip[i], 0, now++);
        pair(count, value == 1 ? nbt::AAK_MEDIA_PREVIOUS : nbt::AAK_MEDIA_NEXT);
    }
    const size_t count = phoneKeys.size();
    g_wheelMediaKeys = false; frame(0xC07, 2, now++); frame(0xC07, 0, now++);
    g_wheelMediaKeys = true; g_mediaKeysToPhone = false;
    frame(0x511, 2, now++); frame(0x511, 0, now++);
    g_mediaKeysToPhone = true; g_sessionActive = false;
    frame(0xC07, 2, now++); frame(0xC07, 0, now++);
    assert(phoneKeys.size() == count);
    g_sessionActive = true;
    clockMs = 14000; onMediaStatus(0x8001, playing, sizeof(playing), NULL);
    clockMs = 20000; onMediaStatus(0x8001, playing, sizeof(playing), NULL);
    frame(0xC30, 0x80, 20010); frame(0xC30, 0x7F, 20020);
    frame(0xC31, 1, 20030); pair(count, nbt::AAK_MEDIA_PREVIOUS);
    frame(0xC31, 1, 25040); frame(0xC31, 0, 25050); pair(count, nbt::AAK_MEDIA_PREVIOUS);
    assert(nbt::nativeMediaCursor(snapshot(25050), 25050) == nbt::kNativeMediaCursorPrev);
    frame(0xC31, 1, 25060); frame(0xC31, 0, 25070); pair(count + 2, nbt::AAK_MEDIA_PREVIOUS);
    // The Native Audio behavior must not change the legacy MOST fallback for other sources.
    g_mediaNative = false;
    frame(0xC31, 1, 30071); frame(0xC31, 0, 30072);
    assert(phoneKeys.size() == count + 4);
    assert(snapshot(30072).cursor == nbt::kNativeMediaCursorNone);
    g_mediaNative = true;
    frame(0xC31, 1, 30080); frame(0xC31, 0, 30090); pair(count + 4, nbt::AAK_MEDIA_NEXT);
    // Current upstream profile policy owns wheel decisions when enabled; do not use the MOST fallback.
    g_useProfileWheelPolicy = true;
    const size_t beforeProfilePress = phoneKeys.size();
    frame(0xC31, 1, 31000); frame(0xC31, 0, 31010);
    assert(phoneKeys.size() == beforeProfilePress);
    assert(g_kombiMedia.directKeys > 0);
    g_useProfileWheelPolicy = false;
    refreshNativeMediaSource(); resetMedia("input fixture ended");
    pair(count + 4, nbt::AAK_MEDIA_NEXT);
    puts("PASS native media input: real C30/C31, remembered buttons after timeout, one pair per press, legacy fallback and C07/511 dispatch gates");
}
'''.replace("BLOCK", block).replace("KEY", source[key_start:key_end]).replace("KNOB", source[knob_start:knob_end])

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
    cpp, exe = work / "input.cpp", work / "input"
    cpp.write_text(input_harness)
    subprocess.run([compiler, "-std=gnu++0x", "-Wall", "-pthread", "-I" + str(ROOT / "src"),
                    "-I" + str(ROOT / "include"), "-I" + str(ROOT / "tests"), str(cpp),
                    str(ROOT / "src/car/ccs_input.cpp"), str(ROOT / "src/most/client.cpp"),
                    str(ROOT / "tests/fake_dsi_client.cpp"), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], cwd=work, check=True)
