// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// The decoders that read phone messages, on crafted and random input. Each case must return: the alarm turns a
// hang into a failure. `make check` also runs this as a 32-bit build, the car's size_t.
#include "aa_proto.hpp"
#include "wireless_setup.hpp"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <unistd.h>
#include <vector>

using namespace nbt;
using proto::pbFirstVarint;

typedef std::vector<uint8_t> Bytes;

template <size_t N>
static Bytes bytes(const uint8_t (&v)[N]) { return Bytes(v, v + N); }

static const uint32_t ABSENT = 0xFFFFFFFFu;

static bool wireless(const Bytes& d, int field, uint64_t& v) { return wpp::findVarintField(&d[0], d.size(), field, v); }

// A field length that used to carry the read position back to the start of the message, so the scan never ended.
static void testLengthsThatWrap() {
    const uint8_t session32[] = { 0x80, 0x01, 0x12, 0xFA, 0xFF, 0xFF, 0xFF, 0x0F };
    assert(pbFirstVarint(bytes(session32), 2, 1) == ABSENT);

    const uint8_t wireless64[] = { 0x12, 0xF5, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x01 };
    uint64_t v = 0;
    assert(!wireless(bytes(wireless64), 1, v));
    assert(wpp::describeFields(wireless64, sizeof(wireless64)) == " <malformed>");

    // The same wrap with a real field in front: what was read before the bad length still counts.
    const uint8_t after_field[] = { 0x08, 0x05, 0x12, 0xFF, 0xFF, 0xFF, 0xFF, 0x0F };
    assert(pbFirstVarint(bytes(after_field), 0, 1) == 5);
    assert(pbFirstVarint(bytes(after_field), 0, 3) == ABSENT);
    assert(wpp::describeFields(after_field, sizeof(after_field)) == " f1=5 <malformed>");
}

static void testFinding() {
    // Field 1 behind a string, a fixed32 and a fixed64.
    const uint8_t later[] = { 0x12, 0x02, 'h', 'i', 0x25, 1, 2, 3, 4, 0x31, 1, 2, 3, 4, 5, 6, 7, 8, 0x08, 0x2A };
    assert(pbFirstVarint(bytes(later), 0, 1) == 42);
    uint64_t v = 0;
    assert(wireless(bytes(later), 1, v) && v == 42);

    // The id in front of a frame is skipped.
    const uint8_t frame[] = { 0x80, 0x01, 0x08, 0x0D, 0x10, 0x96, 0x01 };
    assert(pbFirstVarint(bytes(frame), 2, 1) == 13);
    assert(pbFirstVarint(bytes(frame), 2, 2) == 150);
    assert(pbFirstVarint(bytes(frame), 2, 3) == ABSENT);

    // A field of that number with another wire type doesn't end the search.
    const uint8_t other_wire[] = { 0x0A, 0x01, 'x', 0x08, 0x07 };
    assert(pbFirstVarint(bytes(other_wire), 0, 1) == 7);

    // Over 32 bits: the session reader keeps the low 32, the wireless one the whole value.
    const uint8_t wide[] = { 0x08, 0x85, 0x80, 0x80, 0x80, 0x10 };
    assert(pbFirstVarint(bytes(wide), 0, 1) == 5);
    assert(wireless(bytes(wide), 1, v) && v == 0x100000005ull);

    // Nothing to read.
    assert(pbFirstVarint(Bytes(), 0, 1) == ABSENT);
    assert(pbFirstVarint(bytes(frame), sizeof(frame), 1) == ABSENT);
    assert(pbFirstVarint(bytes(frame), sizeof(frame) + 5, 1) == ABSENT);
}

// Malformed messages: the search ends and reports the field as absent.
static void testMalformed() {
    const uint8_t cut_tag[] = { 0x80 };
    const uint8_t cut_value[] = { 0x08, 0x85 };
    const uint8_t cut_length[] = { 0x0A, 0x05, 'x' };
    const uint8_t cut_fixed32[] = { 0x0D, 1, 2 };
    const uint8_t cut_fixed64[] = { 0x09, 1, 2, 3 };
    const uint8_t field_zero[] = { 0x00, 0x01, 0x08, 0x05 };
    const uint8_t group[] = { 0x0B, 0x08, 0x05 };
    assert(pbFirstVarint(bytes(cut_tag), 0, 1) == ABSENT);
    assert(pbFirstVarint(bytes(cut_value), 0, 1) == ABSENT);
    assert(pbFirstVarint(bytes(cut_length), 0, 1) == ABSENT);
    assert(pbFirstVarint(bytes(cut_fixed32), 0, 1) == ABSENT);
    assert(pbFirstVarint(bytes(cut_fixed64), 0, 1) == ABSENT);
    assert(pbFirstVarint(bytes(field_zero), 0, 1) == ABSENT);
    assert(pbFirstVarint(bytes(group), 0, 1) == ABSENT);
}

static void testDescribe() {
    const uint8_t all[] = { 0x08, 0x2A, 0x12, 0x02, 'h', 'i', 0x1A, 0x02, 0x00, 0xFF, 0x25, 1, 2, 3, 4,
                            0x29, 1, 2, 3, 4, 5, 6, 7, 8 };
    assert(wpp::describeFields(all, sizeof(all)) == " f1=42 f2='hi' f3=<2 bytes> f4=<fixed32> f5=<fixed64>");
    assert(wpp::describeFields(NULL, 0).empty());

    const std::string text(200, 'x');
    Bytes longer;
    longer.push_back(0x0A);
    longer.push_back(0xC8);
    longer.push_back(0x01);
    longer.insert(longer.end(), text.begin(), text.end());
    assert(wpp::describeFields(&longer[0], longer.size()) == " f1='" + text + "'");
}

static void put(Bytes& o, uint64_t v) {
    while (v >= 0x80) { o.push_back((uint8_t)(v | 0x80)); v >>= 7; }
    o.push_back((uint8_t)v);
}

// Every length that lands near the start or end of the message, at 32 and 64 bits, with the tag at each offset.
static void testNearbyLengths() {
    for (int start = 0; start < 4; ++start) {
        for (int back = -16; back <= 16; ++back) {
            for (int bits = 0; bits < 2; ++bits) {
                Bytes d(start, 0x08);
                d.push_back(0x12);
                put(d, bits ? (uint64_t)(int64_t)back : (uint64_t)(uint32_t)back);
                uint64_t v;
                pbFirstVarint(d, 0, 9);
                wireless(d, 9, v);
                wpp::describeFields(&d[0], d.size());
            }
        }
    }
}

static void testRandom() {
    srand(1);
    for (int i = 0; i < 20000; ++i) {
        Bytes d(rand() % 48);
        for (size_t k = 0; k < d.size(); ++k) d[k] = (uint8_t)rand();
        uint64_t v;
        pbFirstVarint(d, 0, 1 + rand() % 8);
        if (!d.empty()) {
            wireless(d, 1 + rand() % 8, v);
            wpp::describeFields(&d[0], d.size());
        }
    }
}

int main() {
    alarm(60);
    testLengthsThatWrap();
    testFinding();
    testMalformed();
    testDescribe();
    testNearbyLengths();
    testRandom();
    printf("test_aa_proto: all passed (size_t %u bits)\n", (unsigned)sizeof(size_t) * 8);
    return 0;
}
