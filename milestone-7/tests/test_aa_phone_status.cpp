// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Host test: aaPhoneInCall, the AA phone status reader (aa_phone_status.hpp). Payloads are synthetic.
#include "aa_phone_status.hpp"

#include <stdio.h>
#include <string>
#include <vector>

using nbt::aaPhoneInCall;

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

typedef std::vector<uint8_t> Bytes;

// One call {state=1, duration=2, number=3}, as field 1 of the status; a thumbnail (field 6) first if asked.
static Bytes call(int state, size_t thumbnail = 0) {
    nbt::proto::ProtoWriter c, status;
    if (thumbnail) c.string(6, std::string(thumbnail, '\xAB'));
    c.integer(1, state);
    c.integer(2, 0);
    c.string(3, "+00 000 0000000");
    status.message(1, c);
    return status.bytes();
}

static bool inCall(const Bytes& b) { return aaPhoneInCall(b.empty() ? NULL : &b[0], b.size()); }

int main() {
    CHECK(inCall(call(4)));    // ringing
    CHECK(inCall(call(1)));    // in call
    CHECK(inCall(call(2)));    // on hold
    CHECK(!inCall(call(3)));   // inactive
    CHECK(!inCall(Bytes()));   // no calls

    Bytes two = call(3);       // one inactive, one on hold
    Bytes held = call(2);
    two.insert(two.end(), held.begin(), held.end());
    CHECK(inCall(two));

    CHECK(inCall(call(1, 16000)));   // a caller thumbnail before the state

    Bytes signal;              // only signal strength (field 2)
    signal.push_back(0x10); signal.push_back(0x04);
    CHECK(!inCall(signal));

    Bytes cut = call(1);       // length runs past the end
    cut.resize(cut.size() - 4);
    CHECK(!inCall(cut));

    if (fails) { printf("test_aa_phone_status: %d failure(s)\n", fails); return 1; }
    printf("test_aa_phone_status OK\n");
    return 0;
}
