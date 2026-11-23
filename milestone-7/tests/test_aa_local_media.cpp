// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Car local media messages 0x8001 / 0x8002 and ProtoWriter::blob.
#include "radio/aa_local_media.hpp"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <vector>

using namespace nbt;

typedef std::vector<uint8_t> Bytes;

static Bytes bytesOf(const uint8_t* p, size_t n) { return Bytes(p, p + n); }

static void testBlob() {
    proto::ProtoWriter w;
    const uint8_t data[] = { 0xAA, 0xBB };
    w.blob(4, data, 2);
    const uint8_t expect[] = { 0x22, 0x02, 0xAA, 0xBB };
    assert(w.bytes() == bytesOf(expect, sizeof expect));

    proto::ProtoWriter e;
    e.blob(4, NULL, 0);
    const uint8_t empty[] = { 0x22, 0x00 };
    assert(e.bytes() == bytesOf(empty, sizeof empty));
}

static void testStatusWithoutActions() {
    const uint8_t expect[] = { 0x80, 0x01, 0x08, 0x02, 0x12, 0x03, 'D', 'A', 'B' };
    assert(localMediaStatus(2, "DAB", std::vector<int>()) == bytesOf(expect, sizeof expect));
}

static void testMetadata() {
    const uint8_t art[] = { 1, 2, 3 };
    const Bytes m = localMediaMetadata("Testfunk Radio", "Probe Artist \xe2\x80\x93 Probe Titel", "Testfunk Radio",
                                       bytesOf(art, sizeof art));
    Bytes expect;
    expect.push_back(0x80); expect.push_back(0x02);
    const char* s[3] = { "Testfunk Radio", "Probe Artist \xe2\x80\x93 Probe Titel", "Testfunk Radio" };
    for (int i = 0; i < 3; ++i) {
        expect.push_back((uint8_t)(((i + 1) << 3) | 2));
        expect.push_back((uint8_t)strlen(s[i]));
        expect.insert(expect.end(), s[i], s[i] + strlen(s[i]));
    }
    expect.push_back(0x22); expect.push_back(3);
    expect.insert(expect.end(), art, art + 3);
    assert(m == expect);
}

static void testMetadataOmitsEmptyFields() {
    const uint8_t expect[] = { 0x80, 0x02, 0x0a, 0x01, 'X' };
    assert(localMediaMetadata("X", "", "", Bytes()) == bytesOf(expect, sizeof expect));
}

int main() {
    testBlob();
    testStatusWithoutActions();
    testMetadata();
    testMetadataOmitsEmptyFields();
    printf("test_aa_local_media OK\n");
    return 0;
}
