// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef NBT_VOLUME_OSD_HPP
#define NBT_VOLUME_OSD_HPP

// Volume overlay: a QNX Screen window of its own (RGBA8888, per-pixel alpha) on LayerManager surface 1281, drawn over
// the Android Auto picture, showing a pill with a speaker glyph, a bar and a percentage. It appears when the volume
// changes and clears itself 1.4 s later, like the stock OSD.
//
// It never touches the video window or surface 1280: its window is named "1281", so LayerManagerService gives it a
// surface of its own (car-verified 2026-11-05, tools/osd1281probe). VideoManager puts that surface on top of 1280 in
// the Android Auto layer while the picture is shown; the layer's hide order holds neither, so the pill leaves with
// the picture. A failure only means "no OSD" - the daemon keeps running. On by default, NBT_VOL_OSD=0 turns it off; the
// value comes from the car's own volume (AudioMaster 0x20/0x0C08), never from a synthetic sweep.

#include <stdint.h>

namespace nbt {

class VolumeOsd {
public:
    VolumeOsd();
    ~VolumeOsd();

    // LayerManager surface of the pill: its window's id string. A window whose id string is a number gets that
    // surface from LayerManagerService on its first post (created if missing, window bound as native content).
    static const int kSurfaceId = 1281;

    // Spawns the drawing thread, which creates a top-level window in a context of its own. parentWindow (the projection
    // window) is only read for the panel size. Returns false (and does nothing) when Screen cannot provide a window.
    bool start(void* ctx, void* parentWindow);
    void stop();

    // Thread-safe: records the new value, repaints and re-arms the hide timer.
    void setPct(int pct);

    // Thread-safe: follow Android Auto's own show/hide (reverse, park assist, driving mode, BACK hold, ...). The surface
    // itself leaves with the picture (VideoManager's render order); hidden here only means no pill is drawn (iDrive
    // shows its own bar) and a pill on screen is cleared, so it is not still there when the picture comes back.
    void setShown(bool shown);

    // True once LayerManager has surface 1281 for this window and it is set up (panel-sized source and destination,
    // visibility 1). Only then may VideoManager put it in the Android Auto layer's render order.
    bool surfaceReady() const { return surfaceReady_; }

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
    volatile bool aaShown_;     // Android Auto is on screen (setShown)
    volatile bool surfaceReady_;   // LayerManager surface 1281 exists for the window and is set up
    void* parent_;
    void* ctxParent_;
};

} // namespace nbt

#endif // NBT_VOLUME_OSD_HPP
