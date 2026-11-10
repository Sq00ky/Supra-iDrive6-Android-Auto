// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Host test: the cluster track title (src/cluster/ric_media.hpp). First the decision logic on iDrive's own frames from
// the car capture, then the worker end to end: a fake frame source plays iDrive, a local TCP server plays the KOMBI.
#include "cluster/ric_media.hpp"
#include "ric_frames.hpp"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
using namespace nbt::riclist;
using namespace ricf;

static SomeipMsg req(uint16_t method, const char* bodyHex, uint16_t client = 0x0001, uint16_t service = 0xB00E) {
    SomeipMsg m;
    m.service = service; m.method = method; m.client = client; m.session = 1; m.sport = 65486; m.type = 0; m.rc = 0;
    m.body = hex(bodyHex);
    return m;
}

// One thumb-wheel step as iDrive sends it: list, three values, commit.
static void group(MediaList& l, std::vector<OutFrame>* out, const char* list = kListBody, const char* shown = kShown) {
    l.onSniffed(req(0x000B, list), out);
    l.onSniffed(req(0x0009, kValue70), out);
    l.onSniffed(req(0x0009, kValue71), out);
    l.onSniffed(req(0x0009, shown), out);
    assert(out->empty());                       // nothing before iDrive's commit
    l.onSniffed(req(0x0004, kCommit), out);
}

static void logicTests() {
    Bytes text, want;
    std::vector<OutFrame> out;

    // nothing playing: iDrive's list is left alone
    {
        MediaList l;
        group(l, &out);
        assert(out.empty() && l.lists() == 1 && l.handle() == 0x64 && l.idriveTitleId() == 0x03000003 && l.open());
    }
    // playing: iDrive's commit is answered with our text, the same list naming it, and a commit
    MediaList l;
    l.setTitle("Brighter", true, &out);
    assert(out.empty());                        // no list from iDrive yet: nothing to answer
    group(l, &out);
    assert(out.size() == 3);
    assert(textFrame(0x64, kDefaultTextIndex, "Brighter", text));
    assert(out[0].method == 0x0003 && out[0].body == text);
    want = hex(kListBody);
    assert(want[65] == 0x03 && want[68] == 0x03);
    want[65] = 0x03; want[66] = 0x00; want[67] = 0x7E; want[68] = 0x01;
    assert(out[1].method == 0x000B && out[1].body == want);
    assert(out[2].method == 0x0004 && out[2].body == hex(kCommit));
    assert(l.answered() == 1 && l.textId() == 0x03007E01);
    // the next wheel step: the text is already there, only list + commit
    out.clear();
    group(l, &out);
    assert(out.size() == 2 && out[0].method == 0x000B && out[0].body == want && out[1].method == 0x0004);
    // a new title while the list is up goes out at once
    out.clear();
    l.setTitle("Complicated", true, &out);
    assert(out.size() == 3 && out[0].method == 0x0003 && out[1].body == want);
    assert(textFrame(0x64, kDefaultTextIndex, "Complicated", text) && out[0].body == text);
    // the same title again: nothing
    out.clear();
    l.setTitle("Complicated", true, &out);
    assert(out.empty());
    // iDrive closes the list: a new title waits for the next list
    l.onSniffed(req(0x0009, kClosed), &out);
    assert(!l.open());
    l.setTitle("Bukkowasu!!", true, &out);
    assert(out.empty());
    group(l, &out);
    assert(out.size() == 3 && out[0].method == 0x0003);
    // paused / stopped: iDrive's next list stands
    out.clear();
    l.setTitle("Bukkowasu!!", false, &out);
    group(l, &out);
    assert(out.empty());
    // playing again with the list up: list + commit (the text is still ours)
    l.setTitle("Bukkowasu!!", true, &out);
    assert(out.size() == 2 && out[0].method == 0x000B);
    // a frame was lost: the text goes out again
    out.clear();
    l.sendFailed();
    group(l, &out);
    assert(out.size() == 3 && out[0].method == 0x0003);
    // our own frames, replies, other services and other handles never trigger anything
    out.clear();
    l.onSniffed(req(0x000B, kListBody, 0x7E01), &out);
    l.onSniffed(req(0x0004, kCommit, 0x7E01), &out);
    l.onSniffed(req(0x0004, kCommit, 0x0001, 0xB006), &out);
    SomeipMsg reply = req(0x0004, kCommit);
    reply.type = 0x80;
    l.onSniffed(reply, &out);
    l.onSniffed(req(0x0004, kCommit), &out);    // a commit with no list before it
    l.onSniffed(req(0x000B, kListBody), &out);
    l.onSniffed(req(0x0004, "000000c8"), &out); // another instance's commit
    assert(out.empty());
    l.onSniffed(req(0x0004, kCommit), &out);
    assert(out.size() == 2);
    // iDrive reaches our text index: ours moves, and the text is sent again under the new id
    out.clear();
    assert(textFrame(0x64, kDefaultTextIndex, "Some Bluetooth title", text));
    SomeipMsg theirs = req(0x0003, kCommit);
    theirs.body = text;
    l.onSniffed(theirs, &out);
    assert(l.moved() == 1 && l.textId() == 0x03007E02);
    group(l, &out);
    assert(out.size() == 3 && out[0].method == 0x0003 && nbt::kombinav::getU32(out[0].body, 4) == 0x03007E02);
    assert(nbt::kombinav::getU32(out[1].body, 65) == 0x03007E02);
    // another kind of list on the same model (not previous / current / next): left alone, also on a title change
    out.clear();
    Bytes other = hex(kListBody);
    other[17 + 25 + 4 + 3] = 0x09;              // first row's icon is not the "previous" icon
    SomeipMsg om = req(0x000B, kCommit);
    om.body = other;
    l.onSniffed(om, &out);
    l.onSniffed(req(0x0009, kShown), &out);
    l.onSniffed(req(0x0004, kCommit), &out);
    l.setTitle("New Song", true, &out);
    assert(out.empty() && l.others() == 1 && l.otherList() == other);
    // an empty title is nothing to show
    {
        MediaList e;
        e.setTitle("", true, &out);
        group(e, &out);
        assert(out.empty());
    }
    // the index moves within its span and wraps
    {
        MediaList w(100);
        for (int i = 0; i < kIndexSpan; ++i) {
            assert(textFrame(0x64, 100 + (uint32_t)i, "x", text));
            SomeipMsg t = req(0x0003, kCommit);
            t.body = text;
            w.onSniffed(t, &out);
        }
        assert(w.textId() == (0x03000000u | 100) && w.moved() == (unsigned)kIndexSpan);
    }
}

// ---- end to end ------------------------------------------------------------------------------------------------
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static std::vector<Bytes> g_queue;                  // frames "on the wire"
static std::vector<OutFrame> g_kombi;               // what the fake KOMBI received
static int g_listen = -1;

class FakeSource : public FrameSource {
public:
    FakeSource() : opens_(0) {}
    virtual bool open(std::string*) {
        pthread_mutex_lock(&g_mu);
        ++opens_;
        pthread_mutex_unlock(&g_mu);
        return true;
    }
    int opens() {
        pthread_mutex_lock(&g_mu);
        const int n = opens_;
        pthread_mutex_unlock(&g_mu);
        return n;
    }
    virtual int read(std::vector<Bytes>* frames) {
        pthread_mutex_lock(&g_mu);
        frames->insert(frames->end(), g_queue.begin(), g_queue.end());
        g_queue.clear();
        pthread_mutex_unlock(&g_mu);
        if (frames->empty()) usleep(10000);
        return (int)frames->size();
    }
    virtual void close() {}

private:
    int opens_;
};

static bool readN(int fd, uint8_t* p, size_t n) {
    size_t got = 0;
    while (got < n) {
        ssize_t r = recv(fd, p + got, n - got, 0);
        if (r <= 0) return false;
        got += (size_t)r;
    }
    return true;
}

static void* kombiThread(void*) {
    int c = accept(g_listen, NULL, NULL);
    if (c < 0) return NULL;
    const Bytes cookie = hex("ffff8000000000080000000001010200");     // the KOMBI opens with a magic cookie
    send(c, &cookie[0], cookie.size(), 0);
    for (;;) {
        uint8_t h[16];
        if (!readN(c, h, 16)) break;
        const uint32_t len = ((uint32_t)h[4] << 24) | ((uint32_t)h[5] << 16) | ((uint32_t)h[6] << 8) | h[7];
        Bytes body(len - 8);
        if (!body.empty() && !readN(c, &body[0], body.size())) break;
        assert(h[0] == 0xB0 && h[1] == 0x0E && h[8] == 0x7E && h[9] == 0x01 && h[14] == 0x00);
        pthread_mutex_lock(&g_mu);
        g_kombi.push_back(OutFrame((uint16_t)((h[2] << 8) | h[3]), body));
        pthread_mutex_unlock(&g_mu);
        uint8_t r[16];
        memcpy(r, h, 16);
        r[4] = r[5] = r[6] = 0; r[7] = 8; r[14] = 0x80; r[15] = 0x00;
        send(c, r, 16, 0);
    }
    close(c);
    return NULL;
}

static size_t kombiCount() {
    pthread_mutex_lock(&g_mu);
    const size_t n = g_kombi.size();
    pthread_mutex_unlock(&g_mu);
    return n;
}
static bool waitFor(size_t n) {
    for (int i = 0; i < 400 && kombiCount() < n; ++i) usleep(10000);
    return kombiCount() >= n;
}

static uint32_t g_seq = 1000;
static void wire(uint16_t method, const char* bodyHex, uint32_t dst, uint16_t port) {
    const Bytes m = someip(0xB00E, method, 0x0001, (uint16_t)g_seq, 0x00, hex(bodyHex));
    const Bytes f = ethTcp(0x7F000002, 65486, dst, port, g_seq, m);
    g_seq += (uint32_t)m.size();
    pthread_mutex_lock(&g_mu);
    g_queue.push_back(f);
    pthread_mutex_unlock(&g_mu);
}
static void wireGroup(uint16_t port) {
    wire(0x000B, kListBody, 0x7F000001, port);
    wire(0x0009, kValue70, 0x7F000001, port);
    wire(0x0009, kValue71, 0x7F000001, port);
    wire(0x0009, kShown, 0x7F000001, port);
    wire(0x0004, kCommit, 0x7F000001, port);
}

static void endToEnd() {
    g_listen = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    assert(bind(g_listen, (struct sockaddr*)&a, sizeof(a)) == 0 && listen(g_listen, 1) == 0);
    socklen_t l = sizeof(a);
    getsockname(g_listen, (struct sockaddr*)&a, &l);
    const uint16_t port = ntohs(a.sin_port);
    pthread_t t;
    assert(pthread_create(&t, NULL, &kombiThread, NULL) == 0);
    char pb[16];
    snprintf(pb, sizeof(pb), "%u", (unsigned)port);
    setenv("NBT_KOMBI_NAV_HOST", "127.0.0.1", 1);
    setenv("NBT_KOMBI_NAV_PORT", pb, 1);

    FakeSource src;
    {
        KombiMedia off;                         // not switched on: no worker, update() is a no-op
        unsetenv("NBT_KOMBI_MEDIA");
        off.start(true, &src);
        off.update("Song", true);
        usleep(50000);
        assert(src.opens() == 0);
        setenv("NBT_KOMBI_MEDIA", "1", 1);
        off.start(false, &src);                 // Bluetooth audio mode: the car shows the track itself
        off.update("Song", true);
        usleep(50000);
        assert(src.opens() == 0);
    }
    assert(!KombiMedia::settingOn(NULL) && !KombiMedia::settingOn("") && !KombiMedia::settingOn("0") &&
           !KombiMedia::settingOn("yes") && KombiMedia::settingOn("1"));

    KombiMedia km;
    km.start(true, &src);
    usleep(50000);
    assert(src.opens() == 0);                     // nothing playing: not listening
    wireGroup(port);                            // iDrive traffic before anything plays is not even read
    km.update("Song A", true);
    for (int i = 0; i < 200 && src.opens() == 0; ++i) usleep(10000);
    assert(src.opens() == 1);
    usleep(100000);
    assert(kombiCount() == 0 || kombiCount() == 3);   // the early group may or may not have been picked up
    wireGroup(port);
    assert(waitFor(3));
    Bytes text, want = hex(kListBody);
    want[65] = 0x03; want[66] = 0x00; want[67] = 0x7E; want[68] = 0x01;
    pthread_mutex_lock(&g_mu);
    assert(textFrame(0x64, kDefaultTextIndex, "Song A", text));
    assert(g_kombi[0].method == 0x0003 && g_kombi[0].body == text);
    assert(g_kombi[1].method == 0x000B && g_kombi[1].body == want);
    assert(g_kombi[2].method == 0x0004 && g_kombi[2].body == hex(kCommit));
    g_kombi.clear();
    pthread_mutex_unlock(&g_mu);
    usleep(300000);                             // let a possible answer to the early group drain
    pthread_mutex_lock(&g_mu);
    g_kombi.clear();
    pthread_mutex_unlock(&g_mu);
    // the driver skips a track: the list is up, the phone reports the new song, it goes out without a new list
    km.update("Song B", true);
    assert(waitFor(3));
    pthread_mutex_lock(&g_mu);
    assert(textFrame(0x64, kDefaultTextIndex, "Song B", text));
    assert(g_kombi[0].method == 0x0003 && g_kombi[0].body == text && g_kombi[1].body == want && g_kombi[2].method == 0x0004);
    g_kombi.clear();
    pthread_mutex_unlock(&g_mu);
    // frames to another host or port are not iDrive's list traffic
    wire(0x000B, kListBody, 0x7F000009, port);
    wire(0x0004, kCommit, 0x7F000009, port);
    wire(0x000B, kListBody, 0x7F000001, (uint16_t)(port + 1));
    wire(0x0004, kCommit, 0x7F000001, (uint16_t)(port + 1));
    usleep(200000);
    assert(kombiCount() == 0);
    // paused: iDrive's list stands
    km.update("Song B", false);
    usleep(50000);
    wireGroup(port);
    usleep(200000);
    assert(kombiCount() == 0);
    assert(km.answered() >= 2);
    km.stop();
    km.stop();
    close(g_listen);
    pthread_join(t, NULL);
}

int main() {
    logicTests();
    endToEnd();
    printf("test_ric_media OK\n");
    return 0;
}
