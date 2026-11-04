// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef NBT_VOLUME_OSD_HPP
#define NBT_VOLUME_OSD_HPP

// Volume overlay: a QNX Screen window of its own (RGBA8888, per-pixel alpha) drawn above the Android Auto
// video surface, showing a pill with a speaker glyph, a bar and a percentage. It appears when the volume
// changes and hides itself 1.4 s later, like the stock OSD.
//
// Nothing here touches the AA video window, the LayerManager surface or the HMI: it creates an extra window
// and posts to it. Every Screen call is logged with its errno, and a failure only means "no OSD" - the daemon
// keeps running. NBT_VOL_OSD=1 enables it; the value comes from the car's own volume (AudioMaster 0x20/0x0C08),
// never from a synthetic sweep.

#include <stdint.h>

namespace nbt {

class VolumeOsd {
public:
    VolumeOsd();
    ~VolumeOsd();

    // Spawns the drawing thread. Returns false (and does nothing) when Screen cannot provide a window.
    // Creates the pill as a CHILD window of the Android Auto window (same Screen context). A top-level window cannot
    // appear on this panel: an unknown id string has no LayerManager surface, and one named "1280" replaces the
    // projection surface outright. Parented to the projection, Screen composites it over the video.
    bool start(void* ctx, void* parentWindow);
    void stop();

    // Thread-safe: records the new value, repaints and re-arms the hide timer.
    void setPct(int pct);

    // True when the window was created (the OSD can then be expected to appear).
    bool ready() const { return ready_; }

private:
    static void* threadThunk(void* self);
    void loop();

    void* ctx_;
    void* win_;
    volatile bool running_;
    volatile bool ready_;
    volatile int pct_;
    volatile bool dirty_;
    void* parent_;
    void* ctxParent_;
};

} // namespace nbt

#endif // NBT_VOLUME_OSD_HPP
