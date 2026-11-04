// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// A DsiClient with no dlopen, for the host tests. tests/fake_dsi_client.cpp implements the same nbt::DsiClient class as
// src/dsi_client.cpp and is chosen at link time (tests/Makefile), so production code gains no hooks and no virtuals. It
// records what the code under test sends, and fake_dsi:: fires events through the class's public trampolines.
#ifndef FAKE_DSI_CLIENT_HPP
#define FAKE_DSI_CLIENT_HPP

#include "dsi_client.hpp"
#include <string>
#include <vector>

namespace fake_dsi {

struct Call {
    enum Kind { kOpen, kNotify, kRequest } kind;
    uint32_t id;                    // attribute or request id
    std::vector<uint8_t> payload;   // of a request
};

extern std::vector<Call> calls;     // in order
extern bool openResult;             // what open() returns
extern std::string openedIface, openedRole;
extern int openedMajor, openedMinor;

void reset();

// Fire events at the client that opened last. No-ops if no client has opened.
void attach();
void data(uint32_t id, const void* payload, uint32_t len);

bool opened();

} // namespace fake_dsi

#endif
