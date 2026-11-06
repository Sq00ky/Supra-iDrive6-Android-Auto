// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef NBT_RHMIAPP_OVERLAY_WATCH_HPP
#define NBT_RHMIAPP_OVERLAY_WATCH_HPP

// "Show iDrive warnings": which iDrive screens Android Auto steps aside for, driven by CDS hmi.graphicalContext.
//
// Blocklist, not allowlist (issue thread, 2026-11-05): the screens a car can raise on its own (Check Control, collision
// / obstacle warnings, climate, the 360 camera, intelligent safety, seat memory, TA/TP, driver profile, notices...) vary
// too much between models to list them. So: any context that appears while Android Auto is on screen, with no driver
// input behind it, is iDrive's own overlay -> step aside at once. Not overlays: our blank state (13), our menu (10),
// contexts listed in NBT_AA_WARN_IGNORE (screens Android Auto already shows itself), anything within 2 s of our own
// iDrive moves (event 52, show, the entry), and GFXIDENTIFIER_INVALID transitions.
//
// Our own states come from their focus notifications (onOwnState), not from hmi.graphicalContext: CDS numbers them the
// framework's way (50002 for 13, 50000 for 10 on the Supra) and reports 13 only as GFXIDENTIFIER_INVALID, so without it
// the watch never saw iDrive come back to us (rhmiapp-37: the safety button pressed twice in a row looked like no change).
// A knob turn or push alone (daemon overlay 5) is driver input only where iDrive's focus was not on our blank state,
// which does nothing with either. takeAside() is the caller's net for a screen that holds iDrive's focus although the
// watch let it through.
//
// While aside, nothing re-holds iDrive (the caller's popup-aside flag). Android Auto comes back once when iDrive has been
// back on the screen it showed under Android Auto (or on 13 / 10) for 2 s - not on a timer, and not during a 681/20
// ping-pong. If the overlay closes onto a different screen, the driver went on in iDrive: stay aside, the Android Auto
// entry brings it back.
//
// Pure logic, no I/O: times are monotonic ms from the caller, unit test tests/test_overlay_watch.cpp.

#include <stdint.h>

namespace nbt {

class OverlayWatch {
public:
    enum Act { NONE, STEP_ASIDE, COME_BACK, DRIVER, BLOCKED };

    OverlayWatch() : nIgnore_(0), cur_(-1), curSince_(0), home_(-1), pend_(-1), pendMs_(0), pendInput_(false),
                     pendKnob_(true), aside_(-1), asideFrom_(-1), asideMs_(0), isAside_(false), ownMs_(0),
                     ownSet_(false) {}

    void addIgnore(int vc) { if (nIgnore_ < kMaxIgnore) ignore_[nIgnore_++] = vc; }

    // We moved iDrive ourselves (event 52, "show", the entry): what follows for 2 s is not a car overlay.
    void own(uint32_t now) { ownMs_ = now; ownSet_ = true; }

    // Driver chose Android Auto through the entry: whatever we stepped aside for is over.
    void reset(uint32_t now) { isAside_ = false; pend_ = -1; own(now); }

    // A hmi.graphicalContext change. invalid: widget GFXIDENTIFIER_INVALID (iDrive between two screens). input: driver
    // input known at the event (knob input included only where knobCounts). aaOnScreen: the daemon has Android Auto up.
    // knobCounts: a knob turn or push alone can explain this screen - false when iDrive's focus was on our blank state.
    void onContext(int vc, bool invalid, bool input, bool aaOnScreen, uint32_t now, bool knobCounts = true) {
        if (invalid || vc <= 0 || vc == cur_) return;
        cur_ = vc;
        curSince_ = now;
        if (isAside_) return;                            // poll() watches for the way back
        if (!aaOnScreen || vc == home_ || isOwn(vc) || (ownSet_ && (uint32_t)(now - ownMs_) < kOwnMs)) {
            home_ = vc;                                  // the screen iDrive shows under / instead of Android Auto
            pend_ = -1;
            return;
        }
        pend_ = vc;
        pendMs_ = now;
        pendInput_ = input;
        pendKnob_ = knobCounts;
    }

    // iDrive's focus is on our blank state (kAaState) or our menu (kMenuState): from that state's focus notification.
    // A pending screen that iDrive left for us again was a flash.
    void onOwnState(int state, uint32_t now) {
        if (state == cur_) return;
        cur_ = state;
        curSince_ = now;
        if (isAside_) return;                            // poll() takes it as the way back
        home_ = state;
        pend_ = -1;
    }

    // Status poll. inputNow: a key or car control in the daemon's input window. blocked: reverse, park assist or a call
    // own the screen. knobNow: only the knob turned or pushed - counts for the pending screen where onContext() allowed it.
    Act poll(uint32_t now, bool aaOnScreen, bool inputNow, bool blocked, bool knobNow = false) {
        if (isAside_) {
            if (aaOnScreen) {
                // Android Auto is up again (entry, phone, daemon): the aside is over. Not in the first 3 s, while the
                // daemon has not taken our "hide" yet.
                if ((uint32_t)(now - asideMs_) > kHideLagMs) isAside_ = false;
                return NONE;
            }
            bool back = cur_ == asideFrom_ || isOwnState(cur_);
            if (back && !blocked && (uint32_t)(now - curSince_) >= kBackMs) {
                isAside_ = false;
                own(now);
                return COME_BACK;
            }
            return NONE;
        }
        if (pend_ < 0) return NONE;
        if (cur_ != pend_) { pend_ = -1; return NONE; }  // gone again before we acted: a flash
        if ((uint32_t)(now - pendMs_) < kSettleMs) return NONE;
        int p = pend_;
        pend_ = -1;
        if (pendInput_ || inputNow || (knobNow && pendKnob_)) { home_ = p; return DRIVER; }
        if (blocked || !aaOnScreen) return BLOCKED;
        aside_ = p;
        asideFrom_ = home_;
        asideMs_ = now;
        isAside_ = true;
        return STEP_ASIDE;
    }

    // The caller saw the current screen hold iDrive's focus over our blank state with no driver input, although the
    // watch let it through (it came up while Android Auto was down, within 2 s of our own event 52, just after a key).
    // Step aside for it like an overlay; it has closed once iDrive is back on 13 / 10 for 2 s. False (nothing done) when
    // the current screen is ours or ignored, or when already aside.
    bool takeAside(uint32_t now) {
        if (isAside_ || cur_ <= 0 || isOwn(cur_)) return false;
        aside_ = cur_;
        asideFrom_ = kAaState;
        asideMs_ = now;
        isAside_ = true;
        pend_ = -1;
        return true;
    }

    bool isAside() const { return isAside_; }
    int aside() const { return aside_; }
    int asideFrom() const { return asideFrom_; }
    int current() const { return cur_; }

    static const int kAaState = 13;     // <hmiState id="13">, our blank state (hmi.graphicalContext: 50002 on the Supra)
    static const int kMenuState = 10;   // <hmiState id="10">, our menu (hmi.graphicalContext: 50000 on the Supra)

private:
    static const int kMaxIgnore = 16;
    static const uint32_t kOwnMs = 2000;      // our own iDrive moves settle
    static const uint32_t kSettleMs = 300;    // a hard key's input reaches the daemon's state file (250 ms tick)
    static const uint32_t kBackMs = 2000;     // back on the old screen this long = the overlay is really gone
    static const uint32_t kHideLagMs = 3000;

    static bool isOwnState(int vc) { return vc == kAaState || vc == kMenuState; }
    bool isOwn(int vc) const {
        if (isOwnState(vc)) return true;
        for (int i = 0; i < nIgnore_; ++i) if (ignore_[i] == vc) return true;
        return false;
    }

    int ignore_[kMaxIgnore];
    int nIgnore_;
    int cur_;
    uint32_t curSince_;
    int home_;
    int pend_;
    uint32_t pendMs_;
    bool pendInput_;
    bool pendKnob_;
    int aside_, asideFrom_;
    uint32_t asideMs_;
    bool isAside_;
    uint32_t ownMs_;
    bool ownSet_;
};

} // namespace nbt

#endif // NBT_RHMIAPP_OVERLAY_WATCH_HPP
