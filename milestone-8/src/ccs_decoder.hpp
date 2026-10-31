// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef CCS_DECODER_HPP
#define CCS_DECODER_HPP

// Pure decoder: iDrive controller (CCS35up, MOST FBlock 0xE5) status messages -> Android Auto input events.
// Car-verified layout (test_controls run 2026-10-22, controller instance 0x03, op Status 0xC):
//   fkt 0xC46 RotaryEncoderState, 8 bytes:
//       [0] message counter  [1..2] BE16 absolute detent position (+1 per clockwise detent, wraps)
//       [3] centre push  [4] joystick direction  [5..6] 0  [7] 0xff
//       direction is a 12-step clock: 0 = centred, 1 = up, 4 = right, 7 = down, 10 = left
//       (seen: 1,7,4,10 for the tilt sequence up, down, right, left)
//       Centre push = byte 3 (1 pressed, 0 released; car-verified 2026-10-23, no separate 'held' value,
//       bytes 5/6 stay 0). Tilt right = 4, left = 10 also verified.
//   fkt 0xC47 PushButtonsState (7 bytes) and 0xC4F PushButtonsState2 (8 bytes, [7] = 7):
//       one byte per hard key, 0 released / 1 pressed (a 1.5 s MEDIA hold stayed 1: no 'held' value). Both carry the same state, so
//       state-diffing de-duplicates them. Slots: 0 MENU  1 BACK  2 OPTIONS  3 COM  4 MEDIA  5 NAV  6 MAP
//       (BMW controller key order MENU, BACK, OPTION, ...). The 2026-10-22 run was read as 1 OPTIONS / 2 BACK, but in
//       every later car log slot 1 was the key pressed and slot 2 never was, and BACK holds did nothing: slots 1/2
//       swapped in v1.6.4. NBT_KEY_BACK / NBT_KEY_OPTIONS still override the keycodes.
//
// Older "CCS" controller protocol (same FBlock 0xE5, instance 0x01 for the front controller), decoded by HmiMain3D's
// CHmiCCSController next to CDevCCS35up (both always created; whichever the controller speaks wins). Layout from the
// firmware (HmiMain3D FUN_02135a40 / FUN_02135c80 / FUN_00242cf4, scratch zbere/e5_variant.md); C10/C20 seen in a
// G30 NBT EVO ID6 capture 2026-10-26 (v1.8.2 knob.log, issue #3):
//   fkt 0xC10 RotaryEncoder, 7 bytes (inst 1): [0] sender 0x01 [1] receiver 0x1D [2] message counter, +1 per message
//       like C46 [0] (capture: 21 22 23 on consecutive detents). 0xFF = controller not configured (firmware): the
//       message is dropped and the next one re-bases; the capture never reaches 0xFF.
//       [3..4] BE16 detent position (higher = clockwise) [5] cluster 0x1E [6] overflow (non-zero: position re-based).
//       Instance 2 (rear) has one more byte before the cluster.
//   fkt 0xC20 PushButton, 7 bytes, one key event per message: [0] sender [1] receiver [2] message counter (0xFF ignore)
//       [3] status 0 released / 1 pressed / 2 held / 3 no change  [4] tilt 1 up 2 right 4 down 8 left (0 = release)
//       [5] cluster 0x00 hard keys / 0x1D tilt / 0x1E knob (sensor 1 = centre push)  [6] sensor: hard-key bit n = the
//       35up slot n above (same HMI key table), so NBT_KEY_<slot> overrides apply to both protocols.
// Everything here is plain C++98 so the host test (tests/test_ccs_decoder.cpp) can replay car logs.

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace nbt {

// Android key codes / Android Auto extended key codes (all declared on input channel 7 in the SDR).
enum {
    AAK_HOME = 3, AAK_BACK = 4, AAK_CALL = 5,
    AAK_DPAD_UP = 19, AAK_DPAD_DOWN = 20, AAK_DPAD_LEFT = 21, AAK_DPAD_RIGHT = 22, AAK_DPAD_CENTER = 23,
    AAK_SEARCH = 84,
    AAK_ROTARY_CONTROLLER = 65536, AAK_MEDIA = 65537, AAK_NAVIGATION = 65538, AAK_TEL = 65540
};

struct CcsEvent {
    int kind;            // 1 = key (keycode, down), 2 = relative (keycode, delta)
    uint32_t keycode;
    int down;
    int delta;
    const char* what;    // human readable source ("MEDIA", "ROTARY", "TILT_UP", ...)
};

enum { CCS_SLOTS = 7 };

class CcsDecoder {
public:
    CcsDecoder() : inst_(0x03), ccsInst_(0x01), invert_(false), lastPos_(-1), tiltKey_(0), pushDown_(false),
                   ccsLastPos_(-1), ccsTilt_(0), ccsPush_(false) {
        memset(slot_, 0, sizeof(slot_));
        memset(ccsSlot_, 0, sizeof(ccsSlot_));
        static const char* const names[CCS_SLOTS] = { "MENU", "BACK", "OPTIONS", "COM", "MEDIA", "NAV", "MAP" };
        static const uint32_t keys[CCS_SLOTS] = { AAK_HOME, AAK_BACK, 0, AAK_CALL, AAK_MEDIA, AAK_NAVIGATION, AAK_NAVIGATION };
        for (int i = 0; i < CCS_SLOTS; ++i) { slotName_[i] = names[i]; slotKey_[i] = keys[i]; }
    }

    void setInstance(uint8_t inst) { inst_ = inst; }
    void setCcsInstance(uint8_t inst) { ccsInst_ = inst; }       // older CCS protocol (fkt C10/C20), default 0x01
    void setInvert(bool inv) { invert_ = inv; }
    void setSlotKey(int slot, uint32_t key) { if (slot >= 0 && slot < CCS_SLOTS) slotKey_[slot] = key; }
    uint32_t slotKey(int slot) const { return (slot >= 0 && slot < CCS_SLOTS) ? slotKey_[slot] : 0; }
    static const char* slotName(int slot) {
        static const char* const names[CCS_SLOTS] = { "MENU", "BACK", "OPTIONS", "COM", "MEDIA", "NAV", "MAP" };
        return (slot >= 0 && slot < CCS_SLOTS) ? names[slot] : "?";
    }
    // Index of a slot by name (case-sensitive upper case), -1 if unknown.
    static int slotByName(const char* n) {
        for (int i = 0; i < CCS_SLOTS; ++i) if (n && !strcmp(n, slotName(i))) return i;
        return -1;
    }

    // 12-step clock direction -> DPAD key (0 = centred / unknown).
    static uint32_t tiltKey(unsigned dir) {
        if (dir == 0 || dir > 12) return 0;
        if (dir == 12 || dir <= 2) return AAK_DPAD_UP;
        if (dir <= 5) return AAK_DPAD_RIGHT;
        if (dir <= 8) return AAK_DPAD_DOWN;
        return AAK_DPAD_LEFT;
    }
    static const char* tiltName(uint32_t key) {
        switch (key) {
            case AAK_DPAD_UP: return "TILT_UP"; case AAK_DPAD_DOWN: return "TILT_DOWN";
            case AAK_DPAD_LEFT: return "TILT_LEFT"; case AAK_DPAD_RIGHT: return "TILT_RIGHT";
            default: return "TILT_?";
        }
    }

    // Feed one MOST message. Returns the number of events written to out (at most max).
    int feed(uint8_t fblock, uint8_t inst, uint16_t fkt, uint8_t op, const uint8_t* d, size_t n,
             CcsEvent* out, int max) {
        int k = 0;
        if (fblock != 0xE5 || op != 0xC || !d) return 0;
        if ((fkt == 0xC10 || fkt == 0xC20) && inst == ccsInst_) return feedCcs(inst, fkt, d, n, out, max);
        if (inst != inst_) return 0;
        if (fkt == 0xC46 && n >= 5) {
            int pos = (d[1] << 8) | d[2];
            if (lastPos_ >= 0 && pos != lastPos_) {
                int delta = (int)(int16_t)(uint16_t)(pos - lastPos_);   // wrap-safe
                if (invert_) delta = -delta;
                if (delta != 0) add(out, max, k, 2, AAK_ROTARY_CONTROLLER, 0, delta, "ROTARY");
            }
            lastPos_ = pos;
            uint32_t tk = tiltKey(d[4]);
            if (tk != tiltKey_) {
                if (tiltKey_) add(out, max, k, 1, tiltKey_, 0, 0, tiltName(tiltKey_));
                if (tk) add(out, max, k, 1, tk, 1, 0, tiltName(tk));
                tiltKey_ = tk;
            }
            bool push = d[3] != 0;
            if (push != pushDown_) {
                add(out, max, k, 1, AAK_DPAD_CENTER, push ? 1 : 0, 0, "PUSH");
                pushDown_ = push;
            }
        } else if ((fkt == 0xC47 || fkt == 0xC4F) && n >= 1) {
            for (size_t i = 0; i < n && i < (size_t)CCS_SLOTS; ++i) {
                bool p = d[i] != 0;
                if (p != slot_[i]) {
                    slot_[i] = p;
                    if (slotKey_[i]) add(out, max, k, 1, slotKey_[i], p ? 1 : 0, 0, slotName_[i]);
                    else add(out, max, k, 1, 0, p ? 1 : 0, 0, slotName_[i]);   // unmapped: keycode 0, caller logs only
                }
            }
        }
        return k;
    }

    // CCS dpad direction bitmask -> AA key.
    static uint32_t ccsTiltKey(unsigned dir) {
        switch (dir) {
            case 1: return AAK_DPAD_UP; case 2: return AAK_DPAD_RIGHT;
            case 4: return AAK_DPAD_DOWN; case 8: return AAK_DPAD_LEFT;
            default: return 0;
        }
    }

private:
    int feedCcs(uint8_t inst, uint16_t fkt, const uint8_t* d, size_t n, CcsEvent* out, int max) {
        int k = 0;
        if (fkt == 0xC10) {
            size_t need = (inst == 1) ? 7 : 8;
            if (n < need) return 0;
            int pos = (d[3] << 8) | d[4];
            uint8_t clus = d[need - 2], ovf = d[need - 1];
            if (d[2] == 0xFF || clus != 0x1E) { ccsLastPos_ = -1; return 0; }
            // [2] changes on every message, so only the overflow flag re-bases (re-basing on a [2] change dropped
            // every detent in v1.8.2 and v1.8.3).
            if (ovf != 0 || ccsLastPos_ < 0) { ccsLastPos_ = pos; return 0; }
            int delta = (int)(int16_t)(uint16_t)(pos - ccsLastPos_);
            ccsLastPos_ = pos;
            if (invert_) delta = -delta;
            if (delta != 0) add(out, max, k, 2, AAK_ROTARY_CONTROLLER, 0, delta, "ROTARY");
        } else if (n >= 7) {
            if (d[2] == 0xFF) return 0;
            uint8_t st = d[3], dir = d[4], clus = d[5], sens = d[6];
            if (st > 2) return 0;                                   // 3 = no change; anything else is invalid
            bool down = st != 0;
            if (clus == 0x1E) {
                if (sens == 1 && down != ccsPush_) { ccsPush_ = down; add(out, max, k, 1, AAK_DPAD_CENTER, down, 0, "PUSH"); }
            } else if (clus == 0x1D) {
                uint32_t tk = (down && dir) ? ccsTiltKey(dir) : 0;
                if (tk != ccsTilt_) {
                    if (ccsTilt_) add(out, max, k, 1, ccsTilt_, 0, 0, tiltName(ccsTilt_));
                    if (tk) add(out, max, k, 1, tk, 1, 0, tiltName(tk));
                    ccsTilt_ = tk;
                }
            } else if (clus == 0x00) {
                for (int b = 0; b < CCS_SLOTS; ++b) {
                    if (sens != (1u << b) || down == ccsSlot_[b]) continue;
                    ccsSlot_[b] = down;
                    add(out, max, k, 1, slotKey_[b], down ? 1 : 0, 0, slotName_[b]);
                }
            }
        }
        return k;
    }

    static void add(CcsEvent* out, int max, int& k, int kind, uint32_t key, int down, int delta, const char* what) {
        if (!out || k >= max) return;
        out[k].kind = kind; out[k].keycode = key; out[k].down = down; out[k].delta = delta; out[k].what = what;
        ++k;
    }

    uint8_t inst_;
    uint8_t ccsInst_;
    bool invert_;
    int lastPos_;
    uint32_t tiltKey_;
    bool pushDown_;
    bool slot_[CCS_SLOTS];       // CCS35up hard keys (C47/C4F)
    bool ccsSlot_[CCS_SLOTS];    // older CCS hard keys (C20); a press on one must not hide the other
    uint32_t slotKey_[CCS_SLOTS];
    const char* slotName_[CCS_SLOTS];
    int ccsLastPos_;
    uint32_t ccsTilt_;
    bool ccsPush_;
};

} // namespace nbt

#endif
