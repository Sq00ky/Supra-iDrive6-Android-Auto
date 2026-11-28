// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#pragma once
// Fantasy MOST payloads and apply helpers shared by the radio tests.
#include "radio/radio_state.hpp"

#include <stdio.h>
#include <string.h>
#include <vector>

using namespace nbt;

typedef std::vector<uint8_t> Bytes;

template <size_t N>
inline Bytes bytes(const uint8_t (&a)[N]) {
    return Bytes(a, a + N);
}

inline void put(Bytes& v, const char* s) {
    v.insert(v.end(), (const uint8_t*)s, (const uint8_t*)s + strlen(s));
}

inline void putStr(Bytes& v, uint8_t cs, const char* s) {
    v.push_back(cs);
    put(v, s);
    v.push_back(0);
}

inline void putU32(Bytes& v, uint32_t x) {
    v.push_back((uint8_t)(x >> 24));
    v.push_back((uint8_t)(x >> 16));
    v.push_back((uint8_t)(x >> 8));
    v.push_back((uint8_t)x);
}

// C13 with a station; unknown4 is the last byte of the group after the SId.
inline Bytes dabStation(uint32_t sid, const char* shortName, const char* longName, uint8_t unknown4 = 0) {
    Bytes v(2, 0);
    putStr(v, 0x02, "Testensemble");
    putStr(v, 0x02, "Testensemble");
    const uint8_t eid[] = {0x10, 0x01, 0xe0};
    v.insert(v.end(), eid, eid + 3);
    putStr(v, 0x02, shortName);
    putStr(v, 0x02, longName);
    putU32(v, sid);
    const uint8_t tail[] = {0x00, 0x00, 0x00, unknown4, 0x03, 0x00};
    v.insert(v.end(), tail, tail + sizeof(tail));
    putStr(v, 0x02, shortName);
    putStr(v, 0x02, longName);
    v.insert(v.end(), 4, 0);
    return v;
}

static const uint8_t kDabTuning[] = {0x00, 0x00, 0x02, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00,
                                     0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x90,
                                     0x00, 0x02, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00};

// C0F; pi < 0 sends no PI.
inline Bytes fmStation(const char* ps, uint32_t freqKHz, int pi) {
    Bytes v;
    putStr(v, 0x01, ps);
    v.push_back(0x01);
    putU32(v, freqKHz);
    v.push_back(pi >= 0 ? 0x01 : 0x00);
    v.push_back(pi >= 0 ? (uint8_t)(pi >> 8) : 0x00);
    v.push_back(pi >= 0 ? (uint8_t)pi : 0x00);
    v.insert(v.end(), 4, 0);
    return v;
}

inline Bytes label(const char* text) {
    Bytes v(7, 0);
    v[4] = 0x01;
    v.push_back(0x02);
    put(v, text);
    v.push_back(0x00);
    v.push_back(0xff);
    return v;
}

inline Bytes radioText(const char* text) {
    Bytes v;
    putStr(v, 0x01, text);
    return v;
}

// C2B as the DAB tuner sends it: title (1) and artist (4), first two fields of each entry empty.
inline Bytes dlPlus(const char* title, const char* artist) {
    Bytes v(2, 0);
    v.push_back(0x01);
    putStr(v, 0x01, "");
    putStr(v, 0x01, "");
    putStr(v, 0x01, title);
    v.push_back(0x04);
    putStr(v, 0x01, "");
    putStr(v, 0x01, "");
    putStr(v, 0x01, artist);
    return v;
}

inline Bytes announce(uint8_t id) {
    const uint8_t a[] = {0x01, id, 0x01};
    return bytes(a);
}

// C43; tag is the last JPEG byte, to tell images apart; copy 0x80 or 0x01 (the tuner sends both).
inline Bytes slide(uint8_t id, uint8_t tag, uint8_t copy = 0x80) {
    const uint8_t a[] = {0x00, copy, id, 0x01, 0xff, 0xd8, 0xff, tag};
    return bytes(a);
}

static const uint8_t kLogo[] = {0x01, 0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a};
static const uint8_t kNoLogo[] = {0x01};
static const uint8_t kNoSlideshow[] = {0x00, 0x00, 0x00};

static const uint8_t kTableDab[] = {0x00, 0x00, 0x00, 0x68, 0x00, 0x01, 0xc1, 0x00};
static const uint8_t kTableDabDisplaced[] = {0x00, 0x00, 0x00, 0x68, 0x04, 0x00, 0xb8, 0x00, 0x01, 0xc1, 0x00};
static const uint8_t kTableDabCall[] = {0x00, 0x00, 0x00, 0x68, 0x05, 0x00, 0x90, 0x00, 0x01, 0xc1, 0x00};
static const uint8_t kTableDabChime[] = {0x00, 0x00, 0x00, 0x68, 0x00, 0x00, 0x84, 0x00, 0x01, 0xc1, 0x00};
static const uint8_t kTableNoAudio[] = {0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x68, 0x01, 0x01, 0xc1, 0x00};
static const uint8_t kTableFm[] = {0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0xc1, 0x00};
static const uint8_t kTableDabAnnouncement[] = {0x00, 0x00, 0x00, 0x3b, 0x00, 0x01, 0xc1, 0x00};
static const uint8_t kTableSdars[] = {0x00, 0x00, 0x00, 0x6b, 0x00, 0x01, 0xc1, 0x00};
static const uint8_t kTableDabFmFollowing[] = {0x00, 0x00, 0x00, 0xd0, 0x00, 0x01, 0xc1, 0x00};

inline bool dabMsg(RadioState& s, uint16_t fkt, const Bytes& m, uint32_t nowMs = 0) {
    return applyMost(s, kMostFBlockDabTuner, fkt, &m[0], m.size(), nowMs);
}

inline bool fmMsg(RadioState& s, uint16_t fkt, const Bytes& m, uint32_t nowMs = 0) {
    return applyMost(s, kMostFBlockFmTuner, fkt, &m[0], m.size(), nowMs);
}

inline bool tableMsg(RadioState& s, const Bytes& m) {
    return applyMost(s, kMostFBlockConnectionMaster, kMostFktConnectionTable, &m[0], m.size(), 0);
}
