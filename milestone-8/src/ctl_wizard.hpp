// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
//
// Controller setup wizard (used by `test_controls -w`): the user is asked to turn the knob, push it, tilt it and press
// each hard key in turn; this class watches the MOST controller messages (FBlock 0xE5, op Status) and works out
// what this car's controller looks like compared with the layout nbt-aad assumes (CcsDecoder defaults, from the
// Supra: instance 0x03, clockwise = position up, hard-key slots MENU BACK OPTIONS COM MEDIA NAV MAP). It then
// produces the nbt-aad.env lines (NBT_KNOB_INST, NBT_KNOB_INVERT, NBT_KEY_<slot>) needed to match, and nothing
// for whatever already matches. Pure logic, no I/O: tests/test_ctl_wizard.cpp drives it with synthetic messages.
#ifndef NBT_CTL_WIZARD_HPP
#define NBT_CTL_WIZARD_HPP

#include "ccs_decoder.hpp"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

namespace nbt {

class CtlWizard {
public:
    enum Step { ROTATE = 0, PUSH, TILT_UP, TILT_RIGHT, KEY_FIRST, STEPS = KEY_FIRST + CCS_SLOTS };
    // Hard-key bytes: C47 has 7, C4F has 8 with [7] a constant 7 (not a key), so only the first CCS_SLOTS count.
    enum { MAX_SLOTS = CCS_SLOTS };

    CtlWizard() : step_(ROTATE), done_(false), inst_(-1), rotSum_(0), invert_(false),
                  pushSeen_(false), tiltUp_(-1), tiltRight_(-1) {
        for (int i = 0; i < 256; ++i) { lastPos_[i] = -1; rotSumBy_[i] = 0; ccsPos_[i] = -1; }
        ccs_ = false;
        ccsHeld_ = false;
        memset(slotDown_, 0, sizeof(slotDown_));
        for (int i = 0; i < CCS_SLOTS; ++i) keySlot_[i] = -1;
        for (int i = 0; i < CCS_SLOTS; ++i) keyInst_[i] = 0;
        lastC46_[0] = lastC46_[1] = lastC46_[2] = 0;
        haveC46_ = false;
    }

    static int stepCount() { return STEPS; }
    // Physical control the user is asked for in step s.
    static const char* prompt(int s) {
        switch (s) {
            case ROTATE: return "Turn the iDrive knob CLOCKWISE 3 clicks";
            case PUSH: return "PUSH the knob down (centre press) and release it";
            case TILT_UP: return "TILT the knob UP (towards the windscreen) and release it";
            case TILT_RIGHT: return "TILT the knob RIGHT and release it";
            default: break;
        }
        static char buf[96];
        int k = s - KEY_FIRST;
        if (k < 0 || k >= CCS_SLOTS) return "?";
        static const char* const label[CCS_SLOTS] = { "MENU", "BACK", "OPTION", "COM (phone)", "MEDIA", "NAV", "MAP" };
        snprintf(buf, sizeof(buf), "Press and release the %s key (no such key: wait to skip)", label[k]);
        return buf;
    }

    // Starts step s: clears its result so a press from the previous step cannot count.
    void begin(int s) { step_ = s; done_ = false; }
    // True once the current step saw what it asked for.
    bool stepDone() const { return done_; }
    // True when no hard key and no tilt/push is currently held (the main loop waits for this between prompts).
    bool released() const {
        for (int i = 0; i < MAX_SLOTS; ++i) if (slotDown_[i]) return false;
        if (ccsHeld_) return false;
        return !(haveC46_ && (lastC46_[0] || lastC46_[1]));
    }

    // Feed one MOST message (any FBlock / instance; only E5 Status messages are used).
    void feed(uint8_t fblock, uint8_t inst, uint16_t fkt, uint8_t op, const uint8_t* d, size_t n) {
        if (fblock != 0xE5 || op != 0xC || !d) return;
        if (fkt == 0xC46 && n >= 5) {
            int pos = (d[1] << 8) | d[2];
            int last = lastPos_[inst];
            lastPos_[inst] = pos;
            if (step_ == ROTATE && !done_ && last >= 0 && pos != last) {
                // Per instance: the one that moves 2+ clicks first is the knob (other E5 devices may also report).
                int& sum = rotSumBy_[inst];
                sum += (int)(int16_t)(uint16_t)(pos - last);
                if (sum >= 2 || sum <= -2) { inst_ = inst; rotSum_ = sum; invert_ = sum < 0; done_ = true; }
            }
            if (inst_ >= 0 && (int)inst != inst_) return;
            uint8_t push = d[3], tilt = d[4];
            if (step_ == PUSH && push && !lastC46_[0]) { pushSeen_ = true; done_ = true; }
            if (step_ == TILT_UP && tilt && !lastC46_[1]) { tiltUp_ = tilt; done_ = true; }
            if (step_ == TILT_RIGHT && tilt && !lastC46_[1]) { tiltRight_ = tilt; done_ = true; }
            lastC46_[0] = push;
            lastC46_[1] = tilt;
            haveC46_ = true;
        } else if (fkt == 0xC10 && n >= 7) {
            // Older CCS protocol knob: [2] message counter (0xFF = not configured) [3..4] BE16 position.
            if (d[2] == 0xFF) return;
            int pos = (d[3] << 8) | d[4];
            int last = ccsPos_[inst];
            ccsPos_[inst] = pos;
            if (step_ == ROTATE && !done_ && last >= 0 && pos != last) {
                int& sum = rotSumBy_[inst];
                sum += (int)(int16_t)(uint16_t)(pos - last);
                if (sum >= 2 || sum <= -2) { inst_ = inst; ccs_ = true; rotSum_ = sum; invert_ = sum < 0; done_ = true; }
            }
        } else if (fkt == 0xC20 && n >= 7) {
            // Older CCS protocol key event: [3] status 0/1/2 (3 = no change) [4] tilt bits [5] cluster [6] sensor.
            if (d[2] == 0xFF || d[3] > 2) return;
            if (inst_ >= 0 && (int)inst != inst_) return;
            bool down = d[3] != 0;
            uint8_t dir = d[4], clus = d[5], sens = d[6];
            ccsHeld_ = down;
            if (!down) return;
            if (step_ == PUSH && clus == 0x1E && sens == 1) { pushSeen_ = true; done_ = true; }
            if (step_ == TILT_UP && clus == 0x1D && dir) { tiltUp_ = 100 + dir; done_ = true; }
            if (step_ == TILT_RIGHT && clus == 0x1D && dir) { tiltRight_ = 100 + dir; done_ = true; }
            if (clus == 0x00 && step_ >= KEY_FIRST && step_ < STEPS && !done_) {
                int bit = -1;
                for (int b = 0; b < 8; ++b) if (sens == (1u << b)) bit = b;
                keySlot_[step_ - KEY_FIRST] = bit >= 0 ? bit : 100 + sens;   // 100+ = not a single bit: noted below
                keyInst_[step_ - KEY_FIRST] = inst;
                done_ = true;
            }
        } else if ((fkt == 0xC47 || fkt == 0xC4F) && n >= 1) {
            for (size_t i = 0; i < n && i < (size_t)MAX_SLOTS; ++i) {
                bool p = d[i] != 0;
                if (p && !slotDown_[i] && step_ >= KEY_FIRST && step_ < STEPS && !done_) {
                    keySlot_[step_ - KEY_FIRST] = (int)i;
                    keyInst_[step_ - KEY_FIRST] = inst;
                    done_ = true;
                }
                slotDown_[i] = p;
            }
        }
    }

    // Results.
    int instance() const { return inst_; }
    bool ccsProtocol() const { return ccs_; }
    bool inverted() const { return invert_; }
    bool pushSeen() const { return pushSeen_; }
    int tiltUpValue() const { return tiltUp_; }
    int tiltRightValue() const { return tiltRight_; }
    int keySlot(int k) const { return (k >= 0 && k < CCS_SLOTS) ? keySlot_[k] : -1; }

    // nbt-aad.env lines for this controller (empty when it matches the defaults) plus human notes.
    void result(std::vector<std::string>& env, std::vector<std::string>& notes) const {
        env.clear();
        notes.clear();
        char line[128];
        CcsDecoder def;
        if (inst_ < 0) {
            notes.push_back("knob: no rotation seen - is the MOST client registered? (send the test_controls log)");
        } else {
            if (!ccs_ && inst_ != 0x03) { snprintf(line, sizeof(line), "NBT_KNOB_INST=%02X", inst_); env.push_back(line); }
            if (ccs_ && inst_ != 0x01) { snprintf(line, sizeof(line), "NBT_KNOB_CCS_INST=%02X", inst_); env.push_back(line); }
            if (ccs_) notes.push_back("controller uses the older CCS protocol (fkt C10/C20): decoded since v1.8.2, layout from "
                                      "the firmware - please send the -l log so it can be confirmed");
            if (invert_) env.push_back("NBT_KNOB_INVERT=1");
        }
        if (!pushSeen_) notes.push_back(ccs_ ? "centre push: no C20 cluster 1E sensor 1 event (send the log)"
                                             : "centre push: not seen in byte 3 of fkt C46 (send the log)");
        // tiltUp_/tiltRight_ >= 100: CCS direction bits (+100), else the 35up 12-step clock value.
        if (tiltUp_ >= 100 && CcsDecoder::ccsTiltKey((unsigned)(tiltUp_ - 100)) != AAK_DPAD_UP) {
            snprintf(line, sizeof(line), "tilt up: CCS direction bits %d (expected 1): tilt orientation differs", tiltUp_ - 100);
            notes.push_back(line);
        }
        if (tiltRight_ >= 100 && CcsDecoder::ccsTiltKey((unsigned)(tiltRight_ - 100)) != AAK_DPAD_RIGHT) {
            snprintf(line, sizeof(line), "tilt right: CCS direction bits %d (expected 2): tilt orientation differs", tiltRight_ - 100);
            notes.push_back(line);
        }
        if (tiltUp_ >= 0 && tiltUp_ < 100 && CcsDecoder::tiltKey((uint8_t)tiltUp_) != AAK_DPAD_UP) {
            snprintf(line, sizeof(line), "tilt up: controller reports direction %d (expected 12/1/2): tilt orientation differs", tiltUp_);
            notes.push_back(line);
        }
        if (tiltRight_ >= 0 && tiltRight_ < 100 && CcsDecoder::tiltKey((uint8_t)tiltRight_) != AAK_DPAD_RIGHT) {
            snprintf(line, sizeof(line), "tilt right: controller reports direction %d (expected 3..5): tilt orientation differs", tiltRight_);
            notes.push_back(line);
        }
        // Which physical key landed in each decoder slot, and the AA key nbt-aad sends for that physical key.
        int physAt[CCS_SLOTS];
        for (int i = 0; i < CCS_SLOTS; ++i) physAt[i] = -1;
        for (int k = 0; k < CCS_SLOTS; ++k) {
            int s = keySlot_[k];
            if (s < 0) continue;
            if (s >= CCS_SLOTS) {
                if (s >= 100) snprintf(line, sizeof(line), "%s key: CCS sensor 0x%02X is not a single bit (send the log)",
                                       CcsDecoder::slotName(k), s - 100);
                else snprintf(line, sizeof(line), "%s key: slot %d is beyond the %d slots nbt-aad decodes (send the log)",
                              CcsDecoder::slotName(k), s, (int)CCS_SLOTS);
                notes.push_back(line);
                continue;
            }
            if (physAt[s] >= 0) {
                snprintf(line, sizeof(line), "%s and %s keys both reported slot %d: one of them was pressed wrongly, re-run",
                         CcsDecoder::slotName(physAt[s]), CcsDecoder::slotName(k), s);
                notes.push_back(line);
                continue;
            }
            physAt[s] = k;
            if (inst_ >= 0 && (int)keyInst_[k] != inst_) {
                snprintf(line, sizeof(line), "%s key: reported by instance %02X, the knob by %02X: nbt-aad reads one instance only",
                         CcsDecoder::slotName(k), keyInst_[k], inst_);
                notes.push_back(line);
            }
        }
        for (int s = 0; s < CCS_SLOTS; ++s) {
            uint32_t want;
            if (physAt[s] >= 0) {
                want = def.slotKey(physAt[s]);          // the AA key this physical key has on the reference car
            } else {
                // Nobody pressed into this slot. If its default physical key turned up in another slot, the slot
                // would send the wrong key: switch it off. Otherwise (key skipped / absent) leave the default.
                bool moved = keySlot_[s] >= 0 && keySlot_[s] != s;
                want = moved ? 0 : def.slotKey(s);
            }
            if (want != def.slotKey(s)) {
                snprintf(line, sizeof(line), "NBT_KEY_%s=%u", CcsDecoder::slotName(s), (unsigned)want);
                env.push_back(line);
            }
        }
        for (int k = 0; k < CCS_SLOTS; ++k)
            if (keySlot_[k] < 0) {
                snprintf(line, sizeof(line), "%s key: not pressed (skipped or not fitted)", CcsDecoder::slotName(k));
                notes.push_back(line);
            }
    }

private:
    int step_;
    bool done_;
    int inst_;
    int lastPos_[256];
    int rotSumBy_[256];
    int ccsPos_[256];
    bool ccs_;          // the knob spoke the older CCS protocol (C10/C20)
    bool ccsHeld_;
    int rotSum_;
    bool invert_;
    bool pushSeen_;
    int tiltUp_, tiltRight_;
    uint8_t lastC46_[3];
    bool haveC46_;
    bool slotDown_[MAX_SLOTS];
    int keySlot_[CCS_SLOTS];
    uint8_t keyInst_[CCS_SLOTS];
};

} // namespace nbt

#endif
