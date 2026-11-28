// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// aa_frames: splitting outgoing messages into frames, and the frame header.
#include "aa_frames.hpp"
#include "aa_proto.hpp"

#include <assert.h>
#include <stdio.h>
#include <vector>

using namespace nbt;

typedef std::vector<uint8_t> Bytes;

static void expectFragment(const AaFragment& f, size_t offset, size_t length, uint8_t type) {
    assert(f.offset == offset);
    assert(f.length == length);
    assert(f.type == type);
}

static void testSplitSmall() {
    std::vector<AaFragment> v = aaSplit(100, 16384);
    assert(v.size() == 1);
    expectFragment(v[0], 0, 100, 3);
}

static void testSplitBoundaries() {
    std::vector<AaFragment> v = aaSplit(16384, 16384);
    assert(v.size() == 1);
    expectFragment(v[0], 0, 16384, 3);

    v = aaSplit(16385, 16384);
    assert(v.size() == 2);
    expectFragment(v[0], 0, 16384, 1);
    expectFragment(v[1], 16384, 1, 2);

    v = aaSplit(40000, 16384);
    assert(v.size() == 3);
    expectFragment(v[0], 0, 16384, 1);
    expectFragment(v[1], 16384, 16384, 0);
    expectFragment(v[2], 32768, 7232, 2);

    assert(aaSplit(0, 16384).empty());
}

// The phone collects a frame's encrypted payload in a fixed 16384-byte buffer and stops reading for good when it
// overflows; its own frames keep header + plaintext at 16128.
static void testFragmentsFitThePhone() {
    const size_t kPhoneBuffer = 16384, kPhoneFrame = 16128, kTlsOverhead = 29;   // TLS 1.2 AES-GCM record
    const size_t sizes[] = { 1, 16120, 16121, 16384, 51806 };                 // 51806: the large art message
    for (size_t s = 0; s < sizeof(sizes) / sizeof(sizes[0]); ++s) {
        std::vector<AaFragment> v = aaSplit(sizes[s], kAaMaxFramePayload);
        size_t sum = 0;
        for (size_t i = 0; i < v.size(); ++i) {
            const size_t header = v[i].type == kAaFrameFirst ? 8 : 4;
            assert(v[i].length + kTlsOverhead <= kPhoneBuffer);
            assert(header + v[i].length <= kPhoneFrame);
            sum += v[i].length;
        }
        assert(sum == sizes[s]);
    }
    std::vector<AaFragment> v = aaSplit(51806, kAaMaxFramePayload);
    assert(v.size() == 4);
    assert(v[0].type == kAaFrameFirst && v[1].type == kAaFrameMiddle && v[3].type == kAaFrameLast);
}

static void testHeader() {
    const uint8_t first[] = { 18, 0x09, 0x12, 0x34, 0x00, 0x00, 0x9C, 0x40 };
    assert(aaFrameHeader(18, 0x09, 0x1234, 40000) == Bytes(first, first + 8));
    const uint8_t bulk[] = { 18, 0x0B, 0x00, 0x05 };
    assert(aaFrameHeader(18, 0x0B, 5, 0) == Bytes(bulk, bulk + 4));
}

int main() {
    testSplitSmall();
    testSplitBoundaries();
    testFragmentsFitThePhone();
    testHeader();
    printf("test_aa_frames: ok\n");
    return 0;
}
