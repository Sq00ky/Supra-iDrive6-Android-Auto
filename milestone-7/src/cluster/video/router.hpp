#pragma once

#include "protocol.hpp"
#include <string.h>

namespace aa_cluster {

struct Hooks {
    void (*setup)(void*, unsigned);
    void (*focus)(void*, unsigned, bool, bool);
    bool (*ready)();
    bool (*resync)();
    unsigned (*state)(bool);
    bool (*queue)(unsigned, const uint8_t*, unsigned, const uint8_t*, unsigned, uint64_t);
    uint32_t (*now)();
};

// Called only by AA's session thread. No decoder or socket I/O happens here.
struct Router {
    Hooks hooks;
    bool enabled, allowed, opened, requested, focused, waiting_idr;
    bool recovery_pending, recovery_asked, deferred, recovery_needed;
    unsigned epoch, config_size;
    uint32_t recovery_at;
    uint8_t config[MaxConfig];

    void reset(bool active, bool clear_config = true) {
        // Changing epochs invalidates queued packets and their eventual ACKs.
        // A new epoch may begin decoding only at an IDR, with cached config.
        epoch = hooks.state(active);
        waiting_idr = true;
        if (clear_config) {
            config_size = 0;
        }
    }

    void advance_recovery(void* session) {
        const uint32_t now = hooks.now();
        // Request a keyframe by briefly releasing focus. The first request is
        // immediate; subsequent attempts wait 10 s, including across wraparound.
        if (recovery_needed && requested && hooks.ready() && !recovery_pending &&
            (!recovery_asked || uint32_t(now - recovery_at) >= 10000)) {
            recovery_needed = false;
            recovery_pending = recovery_asked = true;
            recovery_at = now;
            hooks.focus(session, 2, false, true);
        }
        if (recovery_pending && requested && hooks.ready() && uint32_t(now - recovery_at) >= 500) {
            recovery_pending = false;
            hooks.focus(session, 2, true, true);
        }
    }

    void tick(void* session) {
        if (!enabled) {
            return;
        }
        const bool ready = allowed && hooks.ready();
        if (opened && requested && focused != ready) {
            focused = ready;
            recovery_pending = recovery_needed = false;
            reset(focused, false);
            hooks.resync();
            hooks.focus(session, 2, focused, true);
        }
        if (allowed) {
            advance_recovery(session);
        }
    }

    void allow(void* session, bool on) {
        if (allowed == on) {
            return;
        }
        allowed = on;
        // Re-enable can request a new cluster stream even if the phone replied
        // to our previous focus loss by releasing its own focus request.
        if (on && opened) {
            requested = true;
        }
        if (!on) {
            recovery_pending = recovery_needed = false;
            reset(false, false);
            if (opened && focused) {
                hooks.focus(session, 2, false, true);
            }
            focused = false;
        }
        tick(session);
    }

    void end() {
        if (enabled) {
            reset(false);
        }
        opened = requested = focused = recovery_pending = recovery_asked = recovery_needed = false;
    }

    void recover(void* session) {
        reset(true, false);
        // A second overflow during the 10 s throttle must schedule a later
        // retry. Merely waiting for an IDR can otherwise stall indefinitely.
        if (!recovery_pending) {
            recovery_needed = true;
        }
        advance_recovery(session);
    }

    bool receive(void* session, unsigned channel, const uint8_t* bytes, size_t size) {
        deferred = false;
        if (!enabled || !bytes || size < 2) {
            return false;
        }
        const bool ready = allowed && hooks.ready();
        // All incoming channels tick this on the owning protocol thread. Main
        // video/control traffic continues while the cluster awaits an IDR.
        tick(session);
        if (channel != 2) {
            return false; // Main (6), AUX (4), audio and nav-status untouched.
        }
        const unsigned message = unsigned(bytes[0]) << 8 | bytes[1];
        if (message == 0x8000) { // Media setup request.
            recovery_pending = recovery_asked = recovery_needed = false;
            opened = requested = true;
            focused = ready;
            hooks.setup(session, 2);
            hooks.focus(session, 2, focused, true);
            reset(false);
            return true; // We sent setup; don't let stock send it twice.
        }
        if (message == 0x8007) { // Phone's video focus request.
            requested = field(bytes + 2, size - 2, 2, 2) != 2;
            focused = requested && ready;
            recovery_pending = recovery_needed = false;
            hooks.focus(session, 2, focused, false);
            reset(focused, false);
            return true;
        }
        if (message == 0x8001) { // Media start.
            recovery_needed = false;
            reset(focused);
            return false; // Stock records the session ID used by ACKs.
        }
        if (message == 0x8002) { // Media stop.
            reset(false);
            return false;
        }
        if (message != 0 && message != 1) {
            return false;
        }
        if (!focused || !ready) {
            return false; // Stock still ACKs exactly once.
        }
        if (hooks.resync()) {
            recover(session);
        }
        if (message == 1) { // Codec configuration has no timestamp prefix.
            if (size <= 2 || size - 2 > MaxConfig) {
                reset(true);
                return false;
            }
            config_size = unsigned(size - 2);
            memcpy(config, bytes + 2, config_size);
            return false;
        }
        if (recovery_pending) {
            return false;
        }
        // Video data follows the two-byte message ID and eight-byte timestamp.
        if (size <= 10 || size - 10 > MaxPayload) {
            reset(true, false);
            return false;
        }
        const uint8_t* frame = bytes + 10;
        const unsigned frame_size = unsigned(size - 10);
        if (waiting_idr && !annexb_idr(frame, frame_size)) {
            return false;
        }
        const unsigned prefix_size = waiting_idr ? config_size : 0;
        if (prefix_size > MaxPayload - frame_size ||
            !hooks.queue(epoch, config, prefix_size, frame, frame_size, be64(bytes + 2))) {
            recover(session);
            return false;
        }
        waiting_idr = false;
        // Only queued media defers its ACK; the worker returns its credit after
        // sending the whole access unit, and the session thread sends the ACK.
        deferred = true;
        return false;
    }
};

} // namespace aa_cluster
