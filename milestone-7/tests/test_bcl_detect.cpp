// Copyright (c) 2026 nanaisu - NBT-AAD
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// The BMW Connected (BCL + Etch) link must be recognised; Android Auto wireless frames must not be.
#include <stdio.h>
#include <stdint.h>
#include "wireless_setup.hpp"

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); ++g_fail; } } while (0)

int main() {
    // Seen on a friend's BMW (/dev/btspp0): BCL DATA, src 17, dst 4004, len 60, Etch 0xDEADBEEF.
    static const uint8_t bcl[] = { 0x00, 0x02, 0x00, 0x11, 0x0f, 0xa4, 0x00, 0x3c, 0xde, 0xad, 0xbe, 0xef,
                                   0x00, 0x00, 0x00, 0x34, 0x03, 0x86, 0x54, 0x85 };
    CHECK(nbt::looksLikeBmwBcl(bcl, sizeof(bcl)));
    // Second capture (same Supra): BCL watchdog DATA 5001 -> 5001, payload 13 37 13 37.
    static const uint8_t wd[] = { 0x00, 0x02, 0x13, 0x89, 0x13, 0x89, 0x00, 0x04, 0x13, 0x37, 0x13, 0x37 };
    CHECK(nbt::looksLikeBmwBcl(wd, sizeof(wd)));
    // A generic self-consistent BCL frame without either magic (cmd 1, ports 4004, 2 bytes).
    static const uint8_t gen[] = { 0x00, 0x01, 0x0f, 0xa4, 0x0f, 0xa4, 0x00, 0x02, 0xaa, 0xbb };
    CHECK(nbt::looksLikeBmwBcl(gen, sizeof(gen)));
    // Android Auto WifiStartResponse and WifiStartRequest from the Supra.
    static const uint8_t aa1[] = { 0x00, 0x02, 0x00, 0x07, 0x18, 0x00 };
    static const uint8_t aa2[] = { 0x00, 0x11, 0x00, 0x01, 0x0a, 0x0c, 0x31, 0x37, 0x32, 0x2e, 0x31, 0x36,
                                   0x2e, 0x32, 0x32, 0x34, 0x2e, 0x31, 0x10, 0x9d, 0x29 };
    CHECK(!nbt::looksLikeBmwBcl(aa1, sizeof(aa1)));
    CHECK(!nbt::looksLikeBmwBcl(aa2, sizeof(aa2)));
    // Android Auto WifiInfoRequest-sized frame whose bytes 6..7 happen to equal n - 8 stays AA.
    static const uint8_t aa3[] = { 0x00, 0x06, 0x00, 0x02, 0x0a, 0x00, 0x00, 0x02, 0x10, 0x01 };
    CHECK(!nbt::looksLikeBmwBcl(aa3, sizeof(aa3)));
    CHECK(!nbt::looksLikeBmwBcl(bcl, 0));
    CHECK(!nbt::looksLikeBmwBcl(bcl + 8, 3));   // signature cut short
    printf(g_fail ? "test_bcl_detect: %d FAILED\n" : "test_bcl_detect: all passed\n", g_fail);
    return g_fail ? 1 : 0;
}
