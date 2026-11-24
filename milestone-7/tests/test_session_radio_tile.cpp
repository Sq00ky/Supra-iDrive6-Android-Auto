// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Host test for the radio tile in aa_session: tap changes to car local media messages on channel 18, with the stub
// cryptor and fantasy radio data.
#include "stub_cryptor.hpp"
#include "aa_session.cpp"
#include "radio_fixtures.hpp"
#include <assert.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>

using namespace nbt;

static bool readAll(int fd, uint8_t* p, size_t n, int timeoutMs) {
    size_t got = 0;
    const uint32_t start = monoMs();
    while (got < n) {
        const int left = timeoutMs - (int)(monoMs() - start);
        if (left <= 0) return false;
        struct pollfd pf;
        pf.fd = fd;
        pf.events = POLLIN;
        pf.revents = 0;
        if (poll(&pf, 1, left) <= 0) return false;
        ssize_t r = read(fd, p + got, n - got);
        if (r <= 0) return false;
        got += (size_t)r;
    }
    return true;
}

struct Frame {
    uint8_t channel, flags;
    uint32_t total;   // FIRST frames only
    Bytes payload;
};

static bool readFrame(int fd, int timeoutMs, Frame& f) {
    uint8_t h[4];
    if (!readAll(fd, h, 4, timeoutMs)) return false;
    f.channel = h[0];
    f.flags = h[1];
    f.total = 0;
    if ((f.flags & 3) == kAaFrameFirst) {
        uint8_t t[4];
        if (!readAll(fd, t, 4, timeoutMs)) return false;
        f.total = (uint32_t)t[0] << 24 | (uint32_t)t[1] << 16 | (uint32_t)t[2] << 8 | t[3];
    }
    f.payload.resize((size_t)((h[2] << 8) | h[3]));
    return f.payload.empty() || readAll(fd, &f.payload[0], f.payload.size(), timeoutMs);
}

// The next whole message on channel 18, skipping other channels; frames gets its frames. False on a timeout.
static bool nextTileMessage(int fd, int timeoutMs, Bytes& msg, std::vector<Frame>* frames = NULL) {
    const uint32_t start = monoMs();
    msg.clear();
    if (frames) frames->clear();
    for (;;) {
        const int left = timeoutMs - (int)(monoMs() - start);
        if (left <= 0) return false;
        Frame f;
        if (!readFrame(fd, left, f)) return false;
        if (f.channel != AA_CH_LOCAL_MEDIA) continue;
        msg.insert(msg.end(), f.payload.begin(), f.payload.end());
        if (frames) frames->push_back(f);
        if (f.flags & kAaFrameLast) return true;   // LAST or BULK
    }
}

static uint16_t msgId(const Bytes& m) { return m.size() >= 2 ? (uint16_t)(m[0] << 8 | m[1]) : 0; }

static void dump(const char* what, const Bytes& m) {
    fprintf(stderr, "%s (%u bytes):", what, (unsigned)m.size());
    for (size_t i = 0; i < m.size() && i < 64; ++i) fprintf(stderr, " %02x", m[i]);
    fprintf(stderr, "%s\n", m.size() > 64 ? " ..." : "");
}

static void expectMsg(int fd, int timeoutMs, const Bytes& want) {
    Bytes got;
    const bool ok = nextTileMessage(fd, timeoutMs, got);
    if (!ok || got != want) {
        fprintf(stderr, ok ? "unexpected message on channel 18\n" : "no message on channel 18\n");
        dump("got ", got);
        dump("want", want);
    }
    assert(ok && got == want);
}

static Bytes status(int state, const char* source) { return localMediaStatus(state, source, std::vector<int>()); }

static Bytes metadata(const char* song, const char* artist, const char* album, const Bytes& art = Bytes()) {
    return localMediaMetadata(song, artist, album, art);
}

// The phone, unencrypted: the stub cryptor hands such frames straight to the dispatcher.
static void phoneSends(int fd, uint8_t channel, const Bytes& body) {
    Bytes frame;
    frame.push_back(channel);
    frame.push_back(0x03);
    frame.push_back(0x00);
    frame.push_back((uint8_t)body.size());
    frame.insert(frame.end(), body.begin(), body.end());
    assert(write(fd, &frame[0], frame.size()) == (ssize_t)frame.size());
}

static void openTile(int fd) {
    const uint8_t req[] = {0x00, 0x07, 0x08, 0x00, 0x10, AA_CH_LOCAL_MEDIA};   // ChannelOpenRequest
    phoneSends(fd, AA_CH_LOCAL_MEDIA, bytes(req));
    const uint8_t resp[] = {0x00, 0x08, 0x08, 0x00};
    expectMsg(fd, 2000, bytes(resp));
}

// Media playback status on channel 12: {1: state}.
static Bytes phoneMedia(uint8_t state) {
    const uint8_t m[] = {0x80, 0x01, 0x08, state};
    return bytes(m);
}

static void tapChanged(void* user) { static_cast<AASession*>(user)->postRadioUpdate(); }

static void dab(RadioTap& tap, uint16_t fkt, const Bytes& m) {
    tap.apply(kMostFBlockDabTuner, fkt, &m[0], m.size(), monoMs());
}

static void table(RadioTap& tap, const Bytes& m) {
    tap.apply(kMostFBlockConnectionMaster, kMostFktConnectionTable, &m[0], m.size(), monoMs());
}

static void startOn(AASession* s, int sv[2]) {
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    assert(s->start(sv[0]));
    Frame f;
    assert(readFrame(sv[1], 2000, f));   // the VersionRequest
    assert(f.channel == 0 && f.payload.size() >= 2 && f.payload[1] == 0x01);
}

static AASession* newSession(RadioTap& tap, int sv[2]) {
    AASession* s = new AASession();
    s->setRadioTile(&tap);
    tap.setCallback(&tapChanged, s);
    startOn(s, sv);
    return s;
}

static void endSession(AASession* s, RadioTap& tap, int sv[2]) {
    s->stop();
    close(sv[1]);
    tap.setCallback(NULL, NULL);
    delete s;
}

static void tuneTestfunk(RadioTap& tap) {
    dab(tap, kMostFktDabStation, dabStation(0xD001, "TESTFUNK", "Testfunk Radio"));
    dab(tap, kMostFktDynamicLabel, label("Testlauftext eins"));
    table(tap, bytes(kTableDab));
}

static void expectTestfunk(int fd) {
    expectMsg(fd, 2000, status(2, "DAB"));
    expectMsg(fd, 1000, metadata("Testfunk Radio", "Testlauftext eins", "Testfunk Radio"));
}

// The phone opens channel 18: the tile the tap has goes out at once, status first, and only once.
static void testOpenSendsCurrentTile() {
    RadioTap tap;
    int sv[2];
    AASession* s = newSession(tap, sv);
    tuneTestfunk(tap);
    openTile(sv[1]);
    expectTestfunk(sv[1]);
    Bytes m;
    assert(!nextTileMessage(sv[1], 500, m));
    endSession(s, tap, sv);
}

// Two changes 50 ms apart make one tile, kSettleMs after the last one.
static void testChangesAreCoalesced() {
    RadioTap tap;
    int sv[2];
    AASession* s = newSession(tap, sv);
    tuneTestfunk(tap);
    openTile(sv[1]);
    expectTestfunk(sv[1]);
    const uint32_t t0 = monoMs();
    dab(tap, kMostFktDynamicLabel, label("Testlauftext zwei"));
    usleep(50000);
    const uint32_t tLast = monoMs();
    dab(tap, kMostFktDlPlus, dlPlus("Testtitel", "Testinterpret"));
    const Bytes want = metadata("Testfunk Radio", "Testinterpret \xe2\x80\x93 Testtitel", "Testfunk Radio");
    int metas = 0;
    uint32_t firstAt = 0;
    Bytes m;
    for (;;) {
        const int left = 1200 - (int)(monoMs() - t0);
        if (left <= 0 || !nextTileMessage(sv[1], left, m)) break;
        if (msgId(m) != AA_MSG_LOCAL_MEDIA_METADATA) continue;
        if (++metas > 1) continue;
        firstAt = monoMs();
        if (m != want) dump("got ", m);
        assert(m == want);
    }
    assert(metas == 1);
    assert(firstAt - tLast >= (uint32_t)kSettleMs);
    endSession(s, tap, sv);
}

// While the tuner tunes the last tile stays; the new name goes out once it comes, and without one the fallback after
// kTuneHoldMs.
static void testHoldThenStation() {
    RadioTap tap;
    int sv[2];
    AASession* s = newSession(tap, sv);
    tuneTestfunk(tap);
    openTile(sv[1]);
    expectTestfunk(sv[1]);
    Bytes m;
    const Bytes tuning = bytes(kDabTuning);

    tap.apply(kMostFBlockDabTuner, kMostFktDabStation, &tuning[0], tuning.size(), monoMs());
    assert(!nextTileMessage(sv[1], 2500, m));
    dab(tap, kMostFktDabStation, dabStation(0xD002, "TESTFUNK", ""));
    expectMsg(sv[1], 1000, status(2, "DAB"));
    expectMsg(sv[1], 1000, metadata("TESTFUNK", "", "TESTFUNK"));
    assert(!nextTileMessage(sv[1], 700, m));   // the end of the hold sends nothing more

    const uint32_t t1 = monoMs();
    tap.apply(kMostFBlockDabTuner, kMostFktDabStation, &tuning[0], tuning.size(), t1);
    assert(!nextTileMessage(sv[1], 2500, m));
    expectMsg(sv[1], 1000, status(2, "DAB"));
    assert(monoMs() - t1 >= (uint32_t)kTuneHoldMs);
    expectMsg(sv[1], 1000, metadata("DAB", "", ""));
    endSession(s, tap, sv);
}

// A slide above kAaMaxFramePayload: the metadata go out as FIRST, MIDDLE..., LAST, each within the limit.
static void testLargeArtIsFragmented() {
    RadioTap tap;
    int sv[2];
    AASession* s = newSession(tap, sv);
    Bytes img(51806, 0x5a);
    img[0] = 0xff;
    img[1] = 0xd8;
    img[img.size() - 1] = 0xd9;
    Bytes sl;
    sl.push_back(0x00);
    sl.push_back(0x80);
    sl.push_back(0x07);
    sl.push_back(0x01);
    sl.insert(sl.end(), img.begin(), img.end());
    tuneTestfunk(tap);
    dab(tap, kMostFktSlide, sl);
    openTile(sv[1]);
    expectMsg(sv[1], 2000, status(2, "DAB"));
    Bytes m;
    std::vector<Frame> frames;
    assert(nextTileMessage(sv[1], 2000, m, &frames));
    const Bytes want = metadata("Testfunk Radio", "Testlauftext eins", "Testfunk Radio", img);
    assert(m == want);
    assert(frames.size() >= 3);
    assert(frames.size() == (want.size() + kAaMaxFramePayload - 1) / kAaMaxFramePayload);
    assert(frames[0].total == want.size());
    for (size_t i = 0; i < frames.size(); ++i) {
        const int type = i == 0 ? kAaFrameFirst : (i + 1 == frames.size() ? kAaFrameLast : kAaFrameMiddle);
        assert((frames[i].flags & 3) == type);
        assert(frames[i].flags & kAaFrameEncrypted);
        assert(frames[i].payload.size() <= (size_t)kAaMaxFramePayload);
    }
    endSession(s, tap, sv);
}

// Without channel 18 nothing goes out; after the session a tap change is harmless.
static void testNoSendWithoutChannel() {
    RadioTap tap;
    int sv[2];
    AASession* s = newSession(tap, sv);
    tuneTestfunk(tap);
    dab(tap, kMostFktDlPlus, dlPlus("Testtitel", "Testinterpret"));
    Bytes m;
    assert(!nextTileMessage(sv[1], 1500, m));
    s->stop();
    s->postRadioUpdate();
    dab(tap, kMostFktDynamicLabel, label("Testlauftext drei"));
    close(sv[1]);
    tap.setCallback(NULL, NULL);
    delete s;
}

// Before the first C03 the radio counts as audible unless the phone plays its own media.
static void testPhonePlaybackStopsTile() {
    RadioTap tap;
    int sv[2];
    AASession* s = newSession(tap, sv);
    dab(tap, kMostFktDabStation, dabStation(0xD001, "TESTFUNK", "Testfunk Radio"));
    openTile(sv[1]);
    const Bytes meta = metadata("Testfunk Radio", "", "Testfunk Radio");
    expectMsg(sv[1], 2000, status(2, "DAB"));
    expectMsg(sv[1], 1000, meta);
    phoneSends(sv[1], 12, phoneMedia(2));
    expectMsg(sv[1], 1000, status(1, "DAB"));
    expectMsg(sv[1], 1000, meta);
    phoneSends(sv[1], 12, phoneMedia(3));
    expectMsg(sv[1], 1000, status(2, "DAB"));
    expectMsg(sv[1], 1000, meta);
    endSession(s, tap, sv);
}

// A new session waits for channel 18 again, and forgets the phone's media state.
static void testNewSessionStartsOver() {
    RadioTap tap;
    int sv[2];
    AASession* s = newSession(tap, sv);
    dab(tap, kMostFktDabStation, dabStation(0xD001, "TESTFUNK", "Testfunk Radio"));
    openTile(sv[1]);
    expectMsg(sv[1], 2000, status(2, "DAB"));
    expectMsg(sv[1], 1000, metadata("Testfunk Radio", "", "Testfunk Radio"));
    phoneSends(sv[1], 12, phoneMedia(2));
    expectMsg(sv[1], 1000, status(1, "DAB"));
    expectMsg(sv[1], 1000, metadata("Testfunk Radio", "", "Testfunk Radio"));
    s->stop();
    close(sv[1]);

    startOn(s, sv);
    dab(tap, kMostFktDynamicLabel, label("Testlauftext zwei"));
    Bytes m;
    assert(!nextTileMessage(sv[1], 600, m));
    openTile(sv[1]);
    expectMsg(sv[1], 2000, status(2, "DAB"));
    expectMsg(sv[1], 1000, metadata("Testfunk Radio", "Testlauftext zwei", "Testfunk Radio"));
    endSession(s, tap, sv);
}

// Without a tap, opening channel 18 sends only the response; a tap set while a session runs is used from the opening on.
static void testSetWhileRunning() {
    RadioTap tap;
    int sv[2];
    AASession* s = new AASession();
    tap.setCallback(&tapChanged, s);
    startOn(s, sv);
    tuneTestfunk(tap);
    openTile(sv[1]);
    Bytes m;
    assert(!nextTileMessage(sv[1], 600, m));
    s->stop();
    close(sv[1]);

    startOn(s, sv);
    s->setRadioTile(&tap);
    openTile(sv[1]);
    expectTestfunk(sv[1]);
    endSession(s, tap, sv);
}

int main() {
    signal(SIGPIPE, SIG_IGN);
    setenv("NBT_RX_TIMEOUT_S", "0", 1);
    testOpenSendsCurrentTile();
    testChangesAreCoalesced();
    testHoldThenStation();
    testLargeArtIsFragmented();
    testNoSendWithoutChannel();
    testPhonePlaybackStopsTile();
    testNewSessionStartsOver();
    testSetWhileRunning();
    printf("test_session_radio_tile OK\n");
    return 0;
}
