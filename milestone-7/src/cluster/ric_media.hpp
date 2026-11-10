// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// The Android Auto track title in the instrument cluster's track list (the "Previous title / <track> / Next title"
// list the thumb wheel brings up), J29. Off unless NBT_KOMBI_MEDIA=1.
//
// Why it works the way it does (car findings, 2026-11-10):
//   - iDrive re-sends that list to the KOMBI on every thumb-wheel step (list, three values, commit), always naming
//     its own text model for the middle row. With the phone's music on the car's browser source that text is the
//     last Bluetooth track, and it never changes.
//   - The KOMBI takes list-service frames from a second client (tools/ricprobe), so the title can be replaced. But
//     anything sent on our own schedule is overwritten by iDrive's next list, exactly when the driver looks.
//   - So the daemon listens to iDrive's requests to the KOMBI (cluster/ric_sniff.hpp) and answers each of iDrive's
//     commits with the same list, the middle row pointing at a text model of our own, and a commit. iDrive's own text
//     models are never written: it remembers which one holds which Bluetooth title.
//   - The handle and the list are taken from what iDrive just sent, never from constants. A list that is not that
//     three-row list (another source's list, another car's layout) is left alone.
//
// MediaList is the decision logic (no I/O, host-tested in tests/test_ric_media.cpp); KombiMedia is the worker that
// listens, and sends on its own connection to the KOMBI.
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

class MediaList {
public:
    explicit MediaList(uint32_t index = kDefaultTextIndex, uint16_t ownClient = kOwnClient)
        : own_(ownClient), base_(index), index_(index), show_(false), handle_(0), titleAt_(0), idriveTitle_(0),
          pending_(false), open_(false), sentValid_(false), sentIndex_(0), lists_(0), others_(0), answered_(0),
          moved_(0) {}

    // A message seen on the wire to the KOMBI. Frames to send are appended to out.
    void onSniffed(const SomeipMsg& m, std::vector<OutFrame>* out) {
        if (m.service != kService || m.type != 0x00 || m.client == own_ || m.body.size() < 4) return;
        if (m.method == kMethodText) {
            if (m.body.size() >= 8 && kombinav::getU32(m.body, 4) == textId()) {    // iDrive reached our index
                index_ = base_ + ((index_ - base_ + 1) % (uint32_t)kIndexSpan);
                ++moved_;
            }
        } else if (m.method == kMethodList) {
            if (m.body.size() < 8 || kombinav::getU32(m.body, 4) != kListModel) return;
            size_t at = 0;
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
                ++others_;
            }
        } else if (m.method == kMethodValue) {
            uint32_t v = 0;
            if (shownValue(m.body, &v)) open_ = v == kListModel;
        } else if (m.method == kMethodCommit) {
            if (!pending_ || kombinav::getU32(m.body, 0) != handle_) return;
            pending_ = false;
            if (show_) answer(out);
        }
    }

    // The title to show, and whether Android Auto is what is playing. A new title while the list is up is sent at
    // once (the driver skipped a track and is looking at it); otherwise it waits for iDrive's next list.
    void setTitle(const std::string& title, bool show, std::vector<OutFrame>* out) {
        const bool on = show && !title.empty();
        const bool changed = title != title_ || on != show_;
        title_ = title;
        show_ = on;
        if (changed && show_ && open_ && !pending_ && !list_.empty()) answer(out);
    }

    // A frame did not get through: the text goes out again with the next answer.
    void sendFailed() { sentValid_ = false; }

    uint32_t textId() const { return 0x03000000u | index_; }
    uint32_t handle() const { return handle_; }
    uint32_t idriveTitleId() const { return idriveTitle_; }
    unsigned lists() const { return lists_; }
    unsigned others() const { return others_; }
    unsigned answered() const { return answered_; }
    unsigned moved() const { return moved_; }
    bool open() const { return open_; }
    const Bytes& otherList() const { return other_; }

private:
    void answer(std::vector<OutFrame>* out) {
        if (!sentValid_ || sentIndex_ != index_ || sentTitle_ != title_) {
            Bytes text;
            if (!textFrame(handle_, index_, title_, text)) return;
            out->push_back(OutFrame(kMethodText, text));
            sentValid_ = true;
            sentIndex_ = index_;
            sentTitle_ = title_;
        }
        Bytes l = list_;
        kombinav::putU32(l, titleAt_, textId());
        out->push_back(OutFrame(kMethodList, l));
        out->push_back(OutFrame(kMethodCommit, commitFrame(handle_)));
        ++answered_;
    }

    uint16_t own_;
    uint32_t base_, index_;
    std::string title_;
    bool show_;
    Bytes list_, other_;
    uint32_t handle_;
    size_t titleAt_;
    uint32_t idriveTitle_;
    bool pending_, open_;
    bool sentValid_;
    uint32_t sentIndex_;
    std::string sentTitle_;
    unsigned lists_, others_, answered_, moved_;
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

    // Reads NBT_KOMBI_MEDIA, NBT_KOMBI_MEDIA_IF, NBT_KOMBI_MEDIA_INDEX and NBT_KOMBI_NAV_HOST / _PORT, prints what is
    // in effect and starts the worker (idle until something plays). audioModeOk = the audio mode is not Bluetooth.
    // source: host tests only (not owned).
    void start(bool audioModeOk, FrameSource* source = NULL);
    void stop();
    // The phone's current song and whether it is playing. Any thread, never blocks on I/O.
    void update(const std::string& song, bool playing);
    unsigned answered();

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

    std::string host_, if_;
    uint16_t port_;
    uint32_t ip_, index_;
    FrameSource* src_;
    bool ownSrc_;
    int fd_;
    uint16_t session_;
    unsigned fails_;
};

}  // namespace riclist
}  // namespace nbt

#endif
