// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef TOUCH_FRAME_HPP
#define TOUCH_FRAME_HPP

// Parser for the HMI touch PDU (UDP 160.48.199.115:30501 -> 30567, ident B0 04 80 02).
//
// Datagram: 16-byte header ([0..3] ident, [4..7] BE32 length = datagram size - 8), then 8-byte records:
//     [00][type][flags][seq][4 data bytes]
//   type: 0xC5 first record of a message, 0xC1 middle, 0xC3 last (bit 2 = start, bit 1 = end).
//   seq:  +1 per record (wraps at 256), continuous across datagrams.
//
// One touch message is 12 records. How they are packed into datagrams differs per head unit:
//   Supra (1280x480): the whole message in ONE 112-byte datagram (records C5..C3).
//   NBT Evo 1440x540 (e.g. F20): the SAME message split over 2-4 datagrams of 24..64 bytes
//     (e.g. 32 + 56 + 56 = records 0-1, 2-6, 7-11). Layout from the F-series touch.log capture 2026-10-26.
// So records are reassembled by type/seq first; the touch stream is then identical on both: the data bytes of
// message records 4.. form one contiguous stream (record headers are NOT part of it):
//     stream[0]   flags / rolling counter (top 2 bits count 00,40,80,c0)
//     stream[1]   number of contacts N
//     then N contacts of 6 bytes: x_hi x_lo y_hi y_lo size (state << 4 | id)
//   state: 1 down, 2 move, 3 up. id: stable finger id. x/y in panel pixels (0..1279 x 0..479 on the Supra,
//   0..1439 x 0..539 on a 1440x540 panel). A second finger straddles record boundaries (finger 1 = records 6 and 7).
// Plain C++98, no allocation, so it is host-testable (tests/test_touch_frame.cpp).

#include <stddef.h>
#include <stdint.h>

namespace nbt {

enum {
    kTouchMaxContacts = 5,   // 32 stream bytes - 2 header bytes = 5 contacts of 6 bytes
    kTouchHdrBytes = 16,
    kTouchRecBytes = 8,
    kTouchStreamRec = 4,     // the stream starts in the data of message record 4
    kTouchMaxRecs = 32,
    kTouchMaxMsgs = 4        // completed messages returned per datagram
};

struct TouchContact {
    int id;
    int state;
    int x;
    int y;
    int size;
};

// The data bytes of one reassembled message (4 per record, in record order).
struct TouchMsg {
    unsigned char d[kTouchMaxRecs * 4];
    int recs;
};

// Reads the touch stream of a reassembled message. Returns the number of contacts written to out (0..max), or -1
// if the message is too short to hold the stream header. *declared receives the contact count the message claims.
inline int parseTouchStream(const TouchMsg& m, TouchContact* out, int max, int* declared) {
    if (declared) *declared = 0;
    const int base = kTouchStreamRec * 4;
    const int avail = m.recs * 4 - base;
    if (avail < 2) return -1;
    const unsigned char* s = m.d + base;
    int count = s[1];
    if (declared) *declared = count;
    if (count > kTouchMaxContacts) count = kTouchMaxContacts;
    int k = 0;
    for (int c = 0; c < count && k < max; ++c) {
        const int i0 = 2 + 6 * c;
        if (i0 + 5 >= avail) break;
        TouchContact& t = out[k];
        t.x = (s[i0] << 8) | s[i0 + 1];
        t.y = (s[i0 + 2] << 8) | s[i0 + 3];
        t.size = s[i0 + 4];
        t.state = (s[i0 + 5] >> 4) & 0x0F;
        t.id = s[i0 + 5] & 0x0F;
        if (t.state < 1 || t.state > 3) continue;     // unknown state: ignore this contact
        if (t.id >= kTouchMaxContacts) continue;      // id outside our slot table
        ++k;
    }
    return k;
}

// Joins the records of one message across datagrams. UDP: a lost datagram drops that message; the next start
// record resynchronises.
class TouchReassembler {
public:
    TouchReassembler() : open_(false), lastSeq_(0), dropped_(0) { cur_.recs = 0; }

    // Feeds one datagram. Returns the number of messages it completed (written to out, at most maxMsgs), or -1 if
    // the datagram is not a touch PDU.
    int feed(const unsigned char* buf, size_t n, TouchMsg* out, int maxMsgs) {
        if (!buf || n < (size_t)(kTouchHdrBytes + kTouchRecBytes)) return -1;
        if (buf[0] != 0xB0 || buf[1] != 0x04 || buf[2] != 0x80 || buf[3] != 0x02) return -1;
        const uint32_t len = ((uint32_t)buf[4] << 24) | ((uint32_t)buf[5] << 16) | ((uint32_t)buf[6] << 8) | buf[7];
        if (len + 8 < n && len + 8 >= (uint32_t)(kTouchHdrBytes + kTouchRecBytes)) n = len + 8;   // ignore trailing bytes
        const size_t recs = (n - kTouchHdrBytes) / kTouchRecBytes;
        int done = 0;
        for (size_t r = 0; r < recs; ++r) {
            const unsigned char* rec = buf + kTouchHdrBytes + kTouchRecBytes * r;
            const unsigned char type = rec[1];
            if (rec[0] != 0x00 || (type & 0xC0) != 0xC0) { drop(); return done > 0 ? done : -1; }
            const bool start = (type & 0x04) != 0, end = (type & 0x02) != 0;
            if (start) {
                if (open_) ++dropped_;               // previous message never ended
                open_ = true;
                cur_.recs = 0;
            } else if (!open_) {
                continue;                            // tail of a message whose start we missed
            } else if (rec[3] != (unsigned char)(lastSeq_ + 1)) {
                drop();                              // gap: a datagram was lost
                continue;
            }
            lastSeq_ = rec[3];
            if (cur_.recs >= kTouchMaxRecs) { drop(); continue; }
            for (int i = 0; i < 4; ++i) cur_.d[cur_.recs * 4 + i] = rec[4 + i];
            ++cur_.recs;
            if (end) {
                if (out && done < maxMsgs) out[done++] = cur_;
                open_ = false;
                cur_.recs = 0;
            }
        }
        return done;
    }

    unsigned dropped() const { return dropped_; }

private:
    void drop() { if (open_) ++dropped_; open_ = false; cur_.recs = 0; }

    TouchMsg cur_;
    bool open_;
    unsigned char lastSeq_;
    unsigned dropped_;
};

// Single-datagram convenience (a whole message in one frame, as on the Supra). Returns the contacts of the last
// message the datagram completes, or -1 if it is not a touch PDU or does not complete a message.
inline int parseTouchFrame(const unsigned char* buf, size_t n, TouchContact* out, int max, int* declared) {
    if (declared) *declared = 0;
    TouchReassembler r;
    TouchMsg m[kTouchMaxMsgs];
    const int nm = r.feed(buf, n, m, kTouchMaxMsgs);
    if (nm <= 0) return -1;
    return parseTouchStream(m[nm - 1], out, max, declared);
}

// Test helper: writes a Supra-style touch PDU (112 bytes, 12 records) for the given contacts. counter = stream[0],
// seq0 = sequence number of the first record.
inline void buildTouchFrame(unsigned char* f, const TouchContact* c, int nc, unsigned char counter,
                            unsigned char seq0 = 0) {
    for (int i = 0; i < 112; ++i) f[i] = 0;
    f[0] = 0xB0; f[1] = 0x04; f[2] = 0x80; f[3] = 0x02; f[7] = 0x68;
    for (int r = 0; r < 12; ++r) { f[16 + 8 * r + 1] = (unsigned char)(r == 0 ? 0xc5 : (r == 11 ? 0xc3 : 0xc1));
                                   f[16 + 8 * r + 2] = 0x40; f[16 + 8 * r + 3] = (unsigned char)(seq0 + r); }
    unsigned char s[32];
    for (int i = 0; i < 32; ++i) s[i] = 0;
    s[0] = counter;
    s[1] = (unsigned char)nc;
    for (int k = 0; k < nc && k < kTouchMaxContacts; ++k) {
        const int i0 = 2 + 6 * k;
        s[i0]     = (unsigned char)(c[k].x >> 8);
        s[i0 + 1] = (unsigned char)(c[k].x & 255);
        s[i0 + 2] = (unsigned char)(c[k].y >> 8);
        s[i0 + 3] = (unsigned char)(c[k].y & 255);
        s[i0 + 4] = (unsigned char)c[k].size;
        s[i0 + 5] = (unsigned char)((c[k].state << 4) | (c[k].id & 0x0F));
    }
    for (int i = 0; i < 32; ++i) f[16 + 8 * (kTouchStreamRec + i / 4) + 4 + (i % 4)] = s[i];
}

// Test helper: splits a 12-record PDU (as built above) into datagrams of recsPer[k] records each, like the
// 1440x540 head units do. Datagram k goes to dg + 112 * k, its size to sizes[k]. Returns the datagram count.
inline int splitTouchFrame(const unsigned char* f, const int* recsPer, int parts, unsigned char* dg, int* sizes) {
    int r0 = 0, k = 0;
    for (; k < parts && r0 < 12; ++k) {
        int nr = recsPer[k];
        if (r0 + nr > 12) nr = 12 - r0;
        unsigned char* d = dg + 112 * k;
        for (int i = 0; i < 16; ++i) d[i] = f[i];
        const int sz = 16 + 8 * nr;
        d[4] = 0; d[5] = 0; d[6] = 0; d[7] = (unsigned char)(sz - 8);
        for (int i = 0; i < 8 * nr; ++i) d[16 + i] = f[16 + 8 * r0 + i];
        sizes[k] = sz;
        r0 += nr;
    }
    return k;
}

} // namespace nbt

#endif // TOUCH_FRAME_HPP
