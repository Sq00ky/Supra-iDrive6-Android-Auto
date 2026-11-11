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

static void listModeTests() {
    Bytes text, want;
    std::vector<OutFrame> out;

    // nothing playing: iDrive's list is left alone
    {
        MediaList l(MediaList::kList);
        group(l, &out);
        assert(out.empty() && l.lists() == 1 && l.handle() == 0x64 && l.idriveTitleId() == 0x03000003 && l.open());
    }
    // playing: iDrive's commit is answered with our text, the same list naming it, iDrive's three values, a commit
    MediaList l(MediaList::kList);
    l.setTitle("Brighter", true, &out);
    assert(out.empty());                        // no list from iDrive yet: nothing to answer
    group(l, &out);
    assert(out.size() == 6);
    assert(textFrame(0x64, kDefaultTextIndex, "Brighter", text));
    assert(out[0].method == 0x0003 && out[0].body == text);
    want = hex(kListBody);
    assert(want[65] == 0x03 && want[68] == 0x03);
    want[65] = 0x03; want[66] = 0x00; want[67] = 0x7E; want[68] = 0x01;
    assert(out[1].method == 0x000B && out[1].body == want);
    assert(out[2].method == 0x0009 && out[2].body == hex(kValue70));
    assert(out[3].method == 0x0009 && out[3].body == hex(kValue71));
    assert(out[4].method == 0x0009 && out[4].body == hex(kShown));
    assert(out[5].method == 0x0004 && out[5].body == hex(kCommit));
    assert(l.answered() == 1 && l.textId() == 0x03007E01 && l.shownId() == 0x03007E01);
    // the next wheel step: the text is already there
    out.clear();
    group(l, &out);
    assert(out.size() == 5 && out[0].method == 0x000B && out[0].body == want && out[4].method == 0x0004);
    // a new title while the list is up goes out at once
    out.clear();
    l.setTitle("Complicated", true, &out);
    assert(out.size() == 6 && out[0].method == 0x0003 && out[1].body == want);
    assert(textFrame(0x64, kDefaultTextIndex, "Complicated", text) && out[0].body == text);
    out.clear();
    l.setTitle("Complicated", true, &out);
    assert(out.empty());
    // iDrive closes the list: a new title waits for the next list
    l.onSniffed(req(0x0009, kClosed), &out);
    assert(!l.open());
    l.setTitle("Bukkowasu!!", true, &out);
    assert(out.empty());
    group(l, &out);
    assert(out.size() == 6 && out[0].method == 0x0003);
    // paused / stopped: iDrive's next list stands
    out.clear();
    l.setTitle("Bukkowasu!!", false, &out);
    group(l, &out);
    assert(out.empty());
    l.setTitle("Bukkowasu!!", true, &out);
    assert(out.size() == 5 && out[0].method == 0x000B);
    // a frame was lost: the text goes out again
    out.clear();
    l.sendFailed();
    group(l, &out);
    assert(out.size() == 6 && out[0].method == 0x0003);
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
    assert(out.size() == 2);                    // list + commit: this group carried no values
    // iDrive reaches our text index: ours moves, and the text is sent again under the new id
    out.clear();
    assert(textFrame(0x64, kDefaultTextIndex, "Some Bluetooth title", text));
    SomeipMsg theirs = req(0x0003, kCommit);
    theirs.body = text;
    l.onSniffed(theirs, &out);
    assert(l.moved() == 1 && l.textId() == 0x03007E02);
    group(l, &out);
    assert(out.size() == 6 && out[0].method == 0x0003 && nbt::kombinav::getU32(out[0].body, 4) == 0x03007E02);
    assert(nbt::kombinav::getU32(out[1].body, 65) == 0x03007E02);
    // another kind of list on the same model: left alone, also on a title change
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
    // the index moves within its span and wraps
    {
        MediaList w(MediaList::kList, 100);
        for (int i = 0; i < kIndexSpan; ++i) {
            assert(textFrame(0x64, 100 + (uint32_t)i, "x", text));
            SomeipMsg t = req(0x0003, kCommit);
            t.body = text;
            w.onSniffed(t, &out);
        }
        assert(w.textId() == (0x03000000u | 100) && w.moved() == (unsigned)kIndexSpan);
    }
}

// The default: the title goes into the text model iDrive's own list names for the middle row.
// The thumb-wheel press: iDrive's cursor (value 0x71) and whether the list is up decide previous / next / nothing.
static void wheelTests() {
    static const char* const kCursor0 = "0000006400000071" "13" "00000007" "00000001" "00000071130000";
    std::vector<OutFrame> out;
    const char* why = NULL;
    unsigned row = 9;
    assert(cursorValue(hex(kValue71), &row) && row == 2);
    assert(cursorValue(hex(kCursor0), &row) && row == 0);
    assert(!cursorValue(hex(kValue70), &row) && !cursorValue(hex(kShown), &row) && !cursorValue(hex(kCommit), &row));
    MediaList l;
    assert(l.cursor() == -1 && !l.open());
    assert(wheelPressAction(l.open(), 5000, l.cursor(), &why) == kPressNone && why && *why);
    l.onSniffed(req(0x0009, kValue71), &out);           // a cursor before any track list is nobody's choice
    assert(l.cursor() == -1);
    group(l, &out);                                     // the captured group: one notch down, cursor on row 2
    assert(l.open() && l.cursor() == 2);
    assert(wheelPressAction(l.open(), 5000, l.cursor(), &why) == kPressNext && !*why);
    assert(wheelPressAction(true, kPressSettleMs - 1, 2, &why) == kPressNone && *why);   // the press that opened it
    assert(wheelPressAction(true, kPressSettleMs, 2, &why) == kPressNext);
    l.onSniffed(req(0x000B, kListBody), &out);          // one notch up: the next group carries row 0
    l.onSniffed(req(0x0009, kValue70), &out);
    l.onSniffed(req(0x0009, kCursor0), &out);
    l.onSniffed(req(0x0009, kShown), &out);
    l.onSniffed(req(0x0004, kCommit), &out);
    assert(l.open() && l.cursor() == 0);
    assert(wheelPressAction(l.open(), 5000, l.cursor(), &why) == kPressPrev && !*why);
    assert(wheelPressAction(true, 5000, 1, &why) == kPressNone && *why);
    assert(wheelPressAction(true, 5000, -1, &why) == kPressNone && wheelPressAction(true, 5000, 3, &why) == kPressNone);
    l.onSniffed(req(0x0009, kValue71, 0x7E01), &out);   // our own client's frames are not iDrive's cursor
    assert(l.cursor() == 0);
    l.onSniffed(req(0x0009, kValue71, 0x0001, 0xB006), &out);   // nor is another service
    assert(l.cursor() == 0);
    l.onSniffed(req(0x0009, kClosed), &out);            // the list goes away about 4 s after the last step
    l.onSniffed(req(0x0004, kCommit), &out);
    assert(!l.open());
    assert(wheelPressAction(l.open(), 5000, l.cursor(), &why) == kPressNone && *why);
    l.onSniffed(req(0x000B, "000000640000006b3000000000"), &out);   // another list on the same model
    assert(l.others() == 1 && l.cursor() == -1);
    l.onSniffed(req(0x0009, kValue71), &out);
    l.onSniffed(req(0x0009, kShown), &out);
    assert(l.open() && l.cursor() == -1);               // up, but its cursor is not ours to act on
    assert(wheelPressAction(l.open(), 5000, l.cursor(), &why) == kPressNone);
    assert(out.empty());                                // nothing was playing: none of this sent a frame
}

static void slotModeTests() {
    Bytes text;
    std::vector<OutFrame> out;
    MediaList s;
    assert(s.mode() == MediaList::kSlot);
    // nothing playing, or no list seen yet: nothing is written
    group(s, &out);
    assert(out.empty() && s.lists() == 1 && s.shownId() == 0x03000003);
    MediaList l;
    l.setTitle("Brighter", true, &out);
    assert(out.empty());
    // iDrive's list names text model 0x03000003: our title goes there, with a commit, and nothing else
    group(l, &out);
    assert(out.size() == 2);
    assert(textFrame(0x64, 3, "Brighter", text));
    assert(out[0].method == 0x0003 && out[0].body == text);
    assert(out[1].method == 0x0004 && out[1].body == hex(kCommit));
    assert(l.answered() == 1 && l.shownId() == 0x03000003);
    // further lists: the title is already there, nothing is sent
    out.clear();
    group(l, &out);
    group(l, &out);
    assert(out.empty() && l.answered() == 1);
    // a new title is written at once, whether the list is up or not
    l.setTitle("Complicated", true, &out);
    assert(out.size() == 2 && textFrame(0x64, 3, "Complicated", text) && out[0].body == text);
    out.clear();
    l.onSniffed(req(0x0009, kClosed), &out);
    l.setTitle("Bukkowasu!!", true, &out);
    assert(out.size() == 2 && textFrame(0x64, 3, "Bukkowasu!!", text) && out[0].body == text && out[1].method == 0x0004);
    // iDrive's list names another text model (its Bluetooth track changed): the title follows it
    out.clear();
    Bytes moved = hex(kListBody);
    moved[68] = 0x05;
    SomeipMsg lm = req(0x000B, kCommit);
    lm.body = moved;
    l.onSniffed(lm, &out);
    assert(out.empty());
    l.onSniffed(req(0x0004, kCommit), &out);
    assert(out.size() == 2 && textFrame(0x64, 5, "Bukkowasu!!", text) && out[0].body == text && l.shownId() == 0x03000005);
    // iDrive writes a text of its own into that model: ours goes back in after its next list
    out.clear();
    assert(textFrame(0x64, 5, "A Bluetooth title", text));
    SomeipMsg theirs = req(0x0003, kCommit);
    theirs.body = text;
    l.onSniffed(theirs, &out);
    assert(out.empty());
    l.onSniffed(lm, &out);
    l.onSniffed(req(0x0004, kCommit), &out);
    assert(out.size() == 2 && out[0].method == 0x0003);
    // iDrive's texts in other models are none of our business
    out.clear();
    assert(textFrame(0x64, 9, "Another title", text));
    theirs.body = text;
    l.onSniffed(theirs, &out);
    l.onSniffed(lm, &out);
    l.onSniffed(req(0x0004, kCommit), &out);
    assert(out.empty());
    // paused: nothing is written; playing again with the title still in place: nothing either
    l.setTitle("Bukkowasu!!", false, &out);
    l.setTitle("Other", false, &out);
    l.onSniffed(lm, &out);
    l.onSniffed(req(0x0004, kCommit), &out);
    assert(out.empty());
    l.setTitle("Bukkowasu!!", true, &out);
    assert(out.empty());
    // a frame was lost: written again after the next list
    l.sendFailed();
    l.onSniffed(lm, &out);
    l.onSniffed(req(0x0004, kCommit), &out);
    assert(out.size() == 2);
    // our own frames are not iDrive's
    out.clear();
    theirs.client = 0x7E01;
    assert(textFrame(0x64, 5, "ours", text));
    theirs.body = text;
    l.onSniffed(theirs, &out);
    l.onSniffed(lm, &out);
    l.onSniffed(req(0x0004, kCommit), &out);
    assert(out.empty());
    // another kind of list: nothing is written, also on a title change
    Bytes other = hex(kListBody);
    other[17 + 25 + 4 + 3] = 0x09;
    SomeipMsg om = req(0x000B, kCommit);
    om.body = other;
    l.onSniffed(om, &out);
    l.onSniffed(req(0x0004, kCommit), &out);
    l.setTitle("New Song", true, &out);
    assert(out.empty() && l.others() == 1);
    // an empty title is nothing to show
    MediaList e;
    e.setTitle("", true, &out);
    group(e, &out);
    assert(out.empty());
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
    assert(kombiCount() == 0 || kombiCount() == 2);   // the early group may or may not have been picked up
    wireGroup(port);
    assert(waitFor(2));
    usleep(300000);                             // an answer to the early group, if any, is the same two frames
    Bytes text;
    pthread_mutex_lock(&g_mu);
    assert(g_kombi.size() == 2);                // written once: the second list finds the title in place
    assert(textFrame(0x64, 3, "Song A", text)); // iDrive's own title text model, 0x03000003 in the captured list
    assert(g_kombi[0].method == 0x0003 && g_kombi[0].body == text);
    assert(g_kombi[1].method == 0x0004 && g_kombi[1].body == hex(kCommit));
    g_kombi.clear();
    pthread_mutex_unlock(&g_mu);
    // the driver skips a track: the phone reports the new song and it is written at once
    km.update("Song B", true);
    assert(waitFor(2));
    pthread_mutex_lock(&g_mu);
    assert(textFrame(0x64, 3, "Song B", text));
    assert(g_kombi.size() == 2 && g_kombi[0].method == 0x0003 && g_kombi[0].body == text && g_kombi[1].method == 0x0004);
    g_kombi.clear();
    pthread_mutex_unlock(&g_mu);
    // a thumb-wheel press now: the list has been up for a while with the cursor on row 2 ("Next title")
    {
        std::string why;
        assert(km.wheelPress(&why) == kPressNext && why.empty());
        wire(0x0009, kClosed, 0x7F000001, port);    // iDrive closes the list: a press only brings it back
        wire(0x0004, kCommit, 0x7F000001, port);
        int a = kPressNext;
        for (int i = 0; i < 100 && a != kPressNone; ++i) { usleep(10000); a = km.wheelPress(&why); }
        assert(a == kPressNone && !why.empty());
        wireGroup(port);                            // ... and a press right after it reopened does nothing yet
        for (int i = 0; i < 100 && why != "the cluster list had only just opened"; ++i) { usleep(2000); km.wheelPress(&why); }
        assert(why == "the cluster list had only just opened");
        a = kPressNone;
        for (int i = 0; i < 200 && a != kPressNext; ++i) { usleep(10000); a = km.wheelPress(&why); }
        assert(a == kPressNext);
        usleep(100000);
        assert(kombiCount() == 0);                  // the title was in place: the extra group wrote nothing
    }
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
    assert(km.wheelPress(NULL) == kPressNone);      // stopped: a press is nobody's
    close(g_listen);
    pthread_join(t, NULL);
}

int main() {
    listModeTests();
    wheelTests();
    slotModeTests();
    endToEnd();
    printf("test_ric_media OK\n");
    return 0;
}
