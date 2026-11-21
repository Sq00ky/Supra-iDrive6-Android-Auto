// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef NBT_FAVOURITE_PREVIEW_HPP
#define NBT_FAVOURITE_PREVIEW_HPP
#include <stdint.h>
#include <stddef.h>
#include <pthread.h>

namespace nbt { namespace car {
// Validated against both touch-only recordings from 12 Nov 2026: 15/0C/C02 Status,
// eight bytes, one byte per favourite; 1 touched, 0 released. Never sends commands to the car.
class FavouritePreview {
public:
    enum { kReleaseGraceMs = 450, kMaxPeekMs = 10000 };
    FavouritePreview() : mask_(0), pressedMask_(0), serial_(0), seen_(0), pressedAt_(0), changedAt_(0), armed_(false) {
        pthread_mutex_init(&lock_, NULL);
    }
    ~FavouritePreview() { pthread_mutex_destroy(&lock_); }
    bool feed(uint8_t fb, uint8_t inst, unsigned fkt, unsigned op,
              const uint8_t* data, size_t len, uint32_t now) {
        if (fb != 0x15 || inst != 0x0c || fkt != 0xc02 || op != 0xc || len != 8 || !data) return false;
        unsigned mask = 0,pressed = 0;
        for (unsigned i = 0; i < 8; ++i) {
            if (data[i] > 2) return false;
            if (data[i]) mask |= 1u << i;
            if (data[i]==2)pressed|=1u<<i;
        }
        pthread_mutex_lock(&lock_);
        bool newPress=(pressed&~pressedMask_)!=0;pressedMask_=pressed;
        if (mask != mask_ || newPress) {
            if ((mask && !mask_) || newPress) { ++serial_; pressedAt_ = now; }
            mask_ = mask;
            changedAt_ = now;
        }
        pthread_mutex_unlock(&lock_);
        return true;
    }
    // eligible: AA would otherwise own the picture (no user exit, reverse, camera, etc.).
    // A touch while already in native iDrive must never bring AA back on release.
    bool step(uint32_t now, bool eligible, bool aaOnScreen, bool linkUp) {
        pthread_mutex_lock(&lock_);
        if (!linkUp) mask_ = 0;
        if (!eligible || !linkUp) { armed_ = false; seen_ = serial_; }
        else {
            if (seen_ != serial_) {
                seen_ = serial_;
                armed_ = armed_ || (aaOnScreen && mask_!=0);
            }
            if (armed_ && ((uint32_t)(now - pressedAt_) >= kMaxPeekMs ||
                (!mask_ && (uint32_t)(now - changedAt_) >= kReleaseGraceMs))) armed_ = false;
        }
        bool result = armed_;
        pthread_mutex_unlock(&lock_);
        return result;
    }
    void cancel() {
        pthread_mutex_lock(&lock_);
        armed_ = false; seen_ = serial_;
        pthread_mutex_unlock(&lock_);
    }
private:
    FavouritePreview(const FavouritePreview&);
    FavouritePreview& operator=(const FavouritePreview&);
    pthread_mutex_t lock_;
    unsigned mask_;
    unsigned pressedMask_;
    uint32_t serial_, seen_, pressedAt_, changedAt_;
    bool armed_;
};
} }
#endif
