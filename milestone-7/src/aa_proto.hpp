// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// aa_proto: reading the protobuf messages Android Auto sends. Pure, no I/O, safe on any input.
#ifndef AA_PROTO_HPP
#define AA_PROTO_HPP

#include <stdint.h>
#include <stddef.h>
#include <string>
#include <vector>

namespace nbt {
namespace proto {

// ---- reading ----------------------------------------------------------------------------------------------------

struct PbField {
    uint32_t num;
    uint32_t wire;          // 0 varint, 1 fixed64, 2 length-delimited, 5 fixed32
    uint64_t value;         // wire 0
    const uint8_t* data;    // wire 2
    size_t len;             // wire 2
};

inline bool pbReadVarint(const uint8_t*& p, const uint8_t* end, uint64_t& v) {
    v = 0;
    for (int shift = 0; p < end && shift < 64; shift += 7) {
        uint8_t b = *p++;
        v |= (uint64_t)(b & 0x7F) << shift;
        if (!(b & 0x80)) return true;
    }
    return false;
}

// Walks the fields of one message. next() returns false at the end or on malformed input (then ok() is false).
class PbReader {
public:
    PbReader(const uint8_t* p, size_t n) : p_(p), end_(p + n), ok_(true) {}
    bool next(PbField& f) {
        if (p_ >= end_) return false;
        uint64_t tag;
        if (!pbReadVarint(p_, end_, tag) || (tag >> 3) == 0) return fail();
        f.num = (uint32_t)(tag >> 3);
        f.wire = (uint32_t)(tag & 7);
        f.value = 0; f.data = NULL; f.len = 0;
        if (f.wire == 0) {
            if (!pbReadVarint(p_, end_, f.value)) return fail();
        } else if (f.wire == 1) {
            if (end_ - p_ < 8) return fail();
            p_ += 8;
        } else if (f.wire == 5) {
            if (end_ - p_ < 4) return fail();
            p_ += 4;
        } else if (f.wire == 2) {
            uint64_t n;
            if (!pbReadVarint(p_, end_, n) || n > (uint64_t)(end_ - p_)) return fail();
            f.data = p_; f.len = (size_t)n; p_ += n;
        } else {
            return fail();
        }
        return true;
    }
    bool ok() const { return ok_; }
private:
    bool fail() { ok_ = false; p_ = end_; return false; }
    const uint8_t* p_;
    const uint8_t* end_;
    bool ok_;
};

// A sub-message or string: [data, data + len). present = the field was there with that wire type.
struct PbSpan {
    const uint8_t* data;
    size_t len;
    bool present;
    PbSpan() : data(NULL), len(0), present(false) {}
    PbSpan(const uint8_t* d, size_t n) : data(d), len(n), present(true) {}
};

// First occurrence of field num (any wire type decides, like a reader that takes the first value it sees).
inline bool pbFirst(const PbSpan& m, uint32_t num, PbField& out) {
    if (!m.present) return false;
    PbReader r(m.data, m.len);
    PbField f;
    while (r.next(f)) {
        if (f.num == num) { out = f; return true; }
    }
    return false;
}

// Text check for a length-delimited field: valid UTF-8, no control characters except tab.
inline bool pbLooksLikeText(const uint8_t* p, size_t n) {
    if (!n) return false;
    size_t i = 0;
    while (i < n) {
        uint8_t c = p[i];
        if ((c < 0x20 && c != 9) || c == 0x7F) return false;
        if (c < 0x80) { ++i; continue; }
        size_t k = (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 0;
        if (!k || i + k > n) return false;
        for (size_t j = 1; j < k; ++j)
            if ((p[i + j] & 0xC0) != 0x80) return false;
        i += k;
    }
    return true;
}

// Sub-message at field num (length-delimited and not text), or an empty span.
inline PbSpan pbMsg(const PbSpan& m, uint32_t num) {
    PbField f;
    if (!pbFirst(m, num, f) || f.wire != 2 || pbLooksLikeText(f.data, f.len)) return PbSpan();
    return PbSpan(f.data, f.len);
}

// String at field num; "" when absent or not text.
inline std::string pbText(const PbSpan& m, uint32_t num) {
    PbField f;
    if (!pbFirst(m, num, f) || f.wire != 2 || !pbLooksLikeText(f.data, f.len)) return std::string();
    return std::string((const char*)f.data, f.len);
}

// Varint at field num.
inline bool pbInt(const PbSpan& m, uint32_t num, int64_t& v) {
    PbField f;
    if (!pbFirst(m, num, f) || f.wire != 0) return false;
    v = (int64_t)f.value;
    return true;
}

// First varint numbered num. Unlike pbInt, a field of that number with another wire type doesn't end the search.
inline bool pbFindVarint(const uint8_t* p, size_t n, uint32_t num, uint64_t& v) {
    PbReader r(p, n);
    PbField f;
    while (r.next(f)) {
        if (f.num == num && f.wire == 0) { v = f.value; return true; }
    }
    return false;
}

// pbFindVarint on a received frame from offset off (2 skips the message id), cut to 32 bits.
inline uint32_t pbFirstVarint(const std::vector<uint8_t>& d, size_t off, uint32_t num) {
    uint64_t v;
    if (off >= d.size() || !pbFindVarint(&d[off], d.size() - off, num, v)) return 0xFFFFFFFFu;
    return (uint32_t)v;
}

} // namespace proto
} // namespace nbt

#endif // AA_PROTO_HPP
