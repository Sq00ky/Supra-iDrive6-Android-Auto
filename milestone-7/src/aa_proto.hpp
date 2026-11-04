// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// aa_proto: reading and writing Android Auto's protobuf messages. Pure, no I/O. The reader is safe on any input.
#ifndef AA_PROTO_HPP
#define AA_PROTO_HPP

#include <stdint.h>
#include <stddef.h>
#include <string>
#include <vector>

namespace nbt {
namespace proto {

enum { WIRE_VARINT = 0, WIRE_FIXED64 = 1, WIRE_LENGTH = 2, WIRE_FIXED32 = 5 };

// ---- writing ----------------------------------------------------------------------------------------------------

inline void putVarint(std::vector<uint8_t>& o, uint64_t v) {
    while (v >= 0x80) { o.push_back((uint8_t)(v | 0x80)); v >>= 7; }
    o.push_back((uint8_t)v);
}

class ProtoWriter {
public:
    ProtoWriter() {}
    // A whole Android Auto message: the 2-byte big-endian id, then the fields.
    explicit ProtoWriter(uint16_t messageId) {
        out_.push_back((uint8_t)(messageId >> 8));
        out_.push_back((uint8_t)messageId);
    }
    // Negative values go out sign-extended to 64 bits, as protobuf does for int32.
    void integer(unsigned field, int value) {
        putVarint(out_, (field << 3) | WIRE_VARINT);
        putVarint(out_, (uint64_t)(int64_t)value);
    }
    void unsignedInteger(unsigned field, uint64_t value) {
        putVarint(out_, (field << 3) | WIRE_VARINT);
        putVarint(out_, value);
    }
    void string(unsigned field, const std::string& value) {
        putVarint(out_, (field << 3) | WIRE_LENGTH);
        putVarint(out_, value.size());
        out_.insert(out_.end(), value.begin(), value.end());
    }
    void message(unsigned field, const ProtoWriter& value) {
        putVarint(out_, (field << 3) | WIRE_LENGTH);
        putVarint(out_, value.out_.size());
        out_.insert(out_.end(), value.out_.begin(), value.out_.end());
    }
    void repeated(unsigned field, const std::vector<int>& values) {
        for (size_t i = 0; i < values.size(); ++i) integer(field, values[i]);
    }
    void packed(unsigned field, const std::vector<int>& values) {
        ProtoWriter list;
        for (size_t i = 0; i < values.size(); ++i) putVarint(list.out_, (uint64_t)(int64_t)values[i]);
        message(field, list);
    }
    const std::vector<uint8_t>& bytes() const { return out_; }
private:
    std::vector<uint8_t> out_;
};

// ---- reading ----------------------------------------------------------------------------------------------------

struct PbField {
    uint32_t num;
    uint32_t wire;          // a WIRE_ value
    uint64_t value;         // WIRE_VARINT
    const uint8_t* data;    // WIRE_LENGTH
    size_t len;             // WIRE_LENGTH
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
        if (f.wire == WIRE_VARINT) {
            if (!pbReadVarint(p_, end_, f.value)) return fail();
        } else if (f.wire == WIRE_FIXED64) {
            if (end_ - p_ < 8) return fail();
            p_ += 8;
        } else if (f.wire == WIRE_FIXED32) {
            if (end_ - p_ < 4) return fail();
            p_ += 4;
        } else if (f.wire == WIRE_LENGTH) {
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
    if (!pbFirst(m, num, f) || f.wire != WIRE_LENGTH || pbLooksLikeText(f.data, f.len)) return PbSpan();
    return PbSpan(f.data, f.len);
}

// String at field num; "" when absent or not text.
inline std::string pbText(const PbSpan& m, uint32_t num) {
    PbField f;
    if (!pbFirst(m, num, f) || f.wire != WIRE_LENGTH || !pbLooksLikeText(f.data, f.len)) return std::string();
    return std::string((const char*)f.data, f.len);
}

// Varint at field num.
inline bool pbInt(const PbSpan& m, uint32_t num, int64_t& v) {
    PbField f;
    if (!pbFirst(m, num, f) || f.wire != WIRE_VARINT) return false;
    v = (int64_t)f.value;
    return true;
}

// First varint numbered num. Unlike pbInt, a field of that number with another wire type doesn't end the search.
inline bool pbFindVarint(const uint8_t* p, size_t n, uint32_t num, uint64_t& v) {
    PbReader r(p, n);
    PbField f;
    while (r.next(f)) {
        if (f.num == num && f.wire == WIRE_VARINT) { v = f.value; return true; }
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
