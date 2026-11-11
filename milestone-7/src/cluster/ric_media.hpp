// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// The Android Auto track title in the instrument cluster's track list (the "Previous title / <track> / Next title"
// list the thumb wheel brings up), J29. Off unless NBT_KOMBI_MEDIA=1.
//
// Why it works the way it does (car findings, 2026-11-10):
//   - iDrive re-sends that list to the KOMBI whenever the thumb wheel is turned or pressed and on every skip (list,
//     three values, commit), always naming its own text model for the middle row. With the phone's music on the car's
//     browser source that text is the last Bluetooth track, and it never changes.
//   - The KOMBI takes text frames from a second client and iDrive's next list shows them (tools/ricprobe).
//   - v1.9.59 answered each of iDrive's lists with the same list naming a text model of our own, plus a commit. The
//     KOMBI answered E_OK to all of it and the display kept iDrive's title: a list from a second client is not what
//     gets drawn (or not without more than list + commit).
//   - So the default (mode "slot") does the one thing the car has shown working: the phone's title is written into
//     the text model iDrive's list names for the middle row. iDrive draws it with its next list, and it is written
//     when the title changes, not when the driver looks, so there is nothing to race. Cost: iDrive remembers that
//     model as the text of its last Bluetooth track, so that one track shows the last Android Auto title if it comes
//     back over Bluetooth before the head unit restarts.
//   - Mode "list" (NBT_KOMBI_MEDIA_MODE=list) is the v1.9.59 way with iDrive's three value frames replayed as well,
//     to find out whether they are what makes the KOMBI draw. It never writes iDrive's text models.
//   - Handle, list and text model are taken from what iDrive just sent, never from constants. A list that is not
//     that three-row list (another source's list, another car's layout) is left alone.
//
//   - Thumb-wheel press (v1.9.61): iDrive's skip from that list acts on its Bluetooth player, so nothing happens
//     with the music on the browser source. Each of iDrive's groups carries the list's cursor (value model 0x71:
//     row 0 "Previous title" or row 2 "Next title"), so a press of the wheel (MOST 15/02 fkt C31, car/mfl.hpp) is
//     turned into Android's previous / next key for the row the cluster shows the cursor on - only while the list
//     was already on the display, so the press that merely brings the list up skips nothing. The wheel's rotation
//     is never mapped: it moves iDrive's cursor, and that is all it should do.
//
// MediaList is the decision logic (no I/O, host-tested in tests/test_ric_media.cpp); KombiMedia is the worker that
// listens (cluster/ric_sniff.hpp), and sends on its own connection to the KOMBI.
#ifndef CLUSTER_RIC_MEDIA_HPP
#define CLUSTER_RIC_MEDIA_HPP

#include <pthread.h>
#include <stdint.h>
#include <stddef.h>
#include <string>
#include <vector>

#include "cluster/ric_list.hpp"
#include "cluster/ric_sniff.hpp"

namespace nbt {
namespace riclist {

static const uint16_t kOwnClient = 0x7E01;        // as tools/ricprobe and the route-guidance client; iDrive is 0x0001
static const uint32_t kDefaultTextIndex = 0x7E01; // far above the indices iDrive hands out (one per title it has seen)
enum { kIndexSpan = 16 };                         // ours moves within this many indices if iDrive ever reaches it

struct OutFrame {
    uint16_t method;
    Bytes body;
    OutFrame(uint16_t m, const Bytes& b) : method(m), body(b) {}
};

// What a thumb-wheel press means, from what iDrive last told the KOMBI. listOpen / openForMs: the list is on the
// display and has been for that long (a press that only just opened it does nothing: the driver has not seen the
// cursor yet, and iDrive's group for that press may reach us before or after the press itself). *why says why not.
enum PressAction { kPressNone = 0, kPressPrev = 1, kPressNext = 2 };
enum { kPressSettleMs = 300 };
inline int wheelPressAction(bool listOpen, uint32_t openForMs, int cursor, const char** why) {
    const char* w = "";
    int a = kPressNone;
    if (!listOpen) w = "the cluster list was not open";
    else if (openForMs < (uint32_t)kPressSettleMs) w = "the cluster list had only just opened";
    else if (cursor == 0) a = kPressPrev;
    else if (cursor == 2) a = kPressNext;
    else w = "the cursor is on neither Previous title nor Next title";
    if (why) *why = w;
    return a;
}

class MediaList {
public:
    enum Mode { kSlot = 0, kList = 1 };

    explicit MediaList(Mode mode = kSlot, uint32_t index = kDefaultTextIndex, uint16_t ownClient = kOwnClient)
        : mode_(mode), own_(ownClient), base_(index), index_(index), show_(false), handle_(0), titleAt_(0),
          idriveTitle_(0), pending_(false), open_(false), sentValid_(false), sentId_(0), lists_(0), others_(0),
          answered_(0), moved_(0), cursor_(-1) {}

    // A message seen on the wire to the KOMBI. Frames to send are appended to out.
    void onSniffed(const SomeipMsg& m, std::vector<OutFrame>* out) {
        if (m.service != kService || m.type != 0x00 || m.client == own_ || m.body.size() < 4) return;
        if (m.method == kMethodText) {
            if (m.body.size() < 8) return;
            const uint32_t id = kombinav::getU32(m.body, 4);
            if (mode_ == kList) {
                if (id == textId()) {                           // iDrive reached our index: ours moves
                    index_ = base_ + ((index_ - base_ + 1) % (uint32_t)kIndexSpan);
                    ++moved_;
                }
            } else if (sentValid_ && id == sentId_) {
                sentValid_ = false;                             // iDrive put a text of its own into that model
            }
        } else if (m.method == kMethodList) {
            if (m.body.size() < 8 || kombinav::getU32(m.body, 4) != kListModel) return;
            size_t at = 0;
            values_.clear();
            if (trackListShape(m.body, &at)) {
                list_ = m.body;
                titleAt_ = at;
                handle_ = kombinav::getU32(m.body, 0);
                idriveTitle_ = kombinav::getU32(m.body, at);
                pending_ = true;
                ++lists_;
            } else {                                // another list on the same model: not ours to touch
                other_ = m.body;
                list_.clear();
                pending_ = false;
                cursor_ = -1;                       // that list's cursor is not a previous / next choice
                ++others_;
            }
        } else if (m.method == kMethodValue) {
            uint32_t v = 0;
            if (shownValue(m.body, &v)) open_ = v == kListModel;
            unsigned row = 0;
            if (!list_.empty() && cursorValue(m.body, &row)) cursor_ = (int)row;    // only the track list's cursor
            if (pending_ && values_.size() < 8) values_.push_back(m.body);     // the values of this group
        } else if (m.method == kMethodCommit) {
            if (!pending_ || kombinav::getU32(m.body, 0) != handle_) return;
            pending_ = false;
            if (show_) answer(out);
        }
    }

    // The title to show, and whether Android Auto is what is playing.
    //   slot: a new title is written as soon as iDrive's text model is known, so the next look already shows it.
    //   list: a new title while the list is up is sent at once; otherwise it waits for iDrive's next list.
    void setTitle(const std::string& title, bool show, std::vector<OutFrame>* out) {
        const bool on = show && !title.empty();
        const bool changed = title != title_ || on != show_;
        title_ = title;
        show_ = on;
        if (!changed || !show_ || pending_ || list_.empty()) return;
        if (mode_ == kSlot || open_) answer(out);
    }

    // A frame did not get through: the text goes out again with the next answer.
    void sendFailed() { sentValid_ = false; }

    Mode mode() const { return mode_; }
    uint32_t textId() const { return 0x03000000u | index_; }                // our own text model (list mode)
    uint32_t shownId() const { return mode_ == kSlot ? idriveTitle_ : textId(); }   // where the title goes
    uint32_t handle() const { return handle_; }
    uint32_t idriveTitleId() const { return idriveTitle_; }
    unsigned lists() const { return lists_; }
    unsigned others() const { return others_; }
    unsigned answered() const { return answered_; }
    unsigned moved() const { return moved_; }
    bool open() const { return open_; }
    int cursor() const { return cursor_; }      // the row iDrive's cursor is on (0 previous, 2 next), -1 = not known
    const Bytes& otherList() const { return other_; }

private:
    void answer(std::vector<OutFrame>* out) {
        const uint32_t id = shownId();
        const bool textDue = !sentValid_ || sentId_ != id || sentTitle_ != title_;
        if (mode_ == kSlot && !textDue) return;                 // our title is already in iDrive's text model
        if (textDue) {
            Bytes text;
            if (!textFrame(handle_, id & 0xFFFFFFu, title_, text)) return;
            out->push_back(OutFrame(kMethodText, text));
            sentValid_ = true;
            sentId_ = id;
            sentTitle_ = title_;
        }
        if (mode_ == kList) {
            Bytes l = list_;
            kombinav::putU32(l, titleAt_, id);
            out->push_back(OutFrame(kMethodList, l));
            for (size_t i = 0; i < values_.size(); ++i) out->push_back(OutFrame(kMethodValue, values_[i]));
        }
        out->push_back(OutFrame(kMethodCommit, commitFrame(handle_)));
        ++answered_;
    }

    Mode mode_;
    uint16_t own_;
    uint32_t base_, index_;
    std::string title_;
    bool show_;
    Bytes list_, other_;
    std::vector<Bytes> values_;
    uint32_t handle_;
    size_t titleAt_;
    uint32_t idriveTitle_;
    bool pending_, open_;
    bool sentValid_;
    uint32_t sentId_;
    std::string sentTitle_;
    unsigned lists_, others_, answered_, moved_;
    int cursor_;
};

// Where the frames come from: /dev/bpf on the car, a fake in the host test.
class FrameSource {
public:
    virtual ~FrameSource() {}
    virtual bool open(std::string* why) = 0;
    // Waits a short while (about 200 ms at most) and appends whole Ethernet frames. Below 0 = the source is gone.
    virtual int read(std::vector<Bytes>* frames) = 0;
    virtual void close() = 0;
};

class KombiMedia {
public:
    KombiMedia();
    ~KombiMedia();

    static bool settingOn(const char* env);     // NBT_KOMBI_MEDIA: only "1" turns it on

    // Reads NBT_KOMBI_MEDIA, NBT_KOMBI_MEDIA_MODE, _IF, _INDEX and NBT_KOMBI_NAV_HOST / _PORT, prints what is
    // in effect and starts the worker (idle until something plays). audioModeOk = the audio mode is not Bluetooth.
    // source: host tests only (not owned).
    void start(bool audioModeOk, FrameSource* source = NULL);
    void stop();
    // The phone's current song and whether it is playing. Any thread, never blocks on I/O.
    void update(const std::string& song, bool playing);
    unsigned answered();
    // A press of the thumb wheel just arrived (any thread): kPressPrev / kPressNext when the cluster list is up with
    // its cursor on that row, kPressNone otherwise (*why then says why).
    int wheelPress(std::string* why);

private:
    static void* workerStub(void* self);
    void worker();
    bool sendAll(const std::vector<OutFrame>& frames, std::string* why);
    void closeLink();

    pthread_t thread_;
    pthread_mutex_t mu_;
    pthread_cond_t cv_;
    bool running_, stop_;
    std::string title_;
    bool show_;
    unsigned answered_;
    bool listOpen_;             // under mu_: what the worker last saw of iDrive's list, for wheelPress()
    uint32_t openSinceMs_;
    int cursor_;

    std::string host_, if_;
    uint16_t port_;
    uint32_t ip_, index_;
    MediaList::Mode mode_;
    FrameSource* src_;
    bool ownSrc_;
    int fd_;
    uint16_t session_;
    unsigned fails_;
};

}  // namespace riclist
}  // namespace nbt

#endif
