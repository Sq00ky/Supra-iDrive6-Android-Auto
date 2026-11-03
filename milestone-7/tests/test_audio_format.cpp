// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Host test: offered AudioConfigs read back from the SDR we send, and the phone's choice in AV START_INDICATION.
#include "sdr_builder.hpp"
#include "dhu_sdp.h"
#include <stdio.h>
#include <stdlib.h>
#include <vector>

using namespace nbt;

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

static bool is(const AaAudioConfig& c, uint32_t rate, uint32_t bits, uint32_t ch) {
    return c.sample_rate == rate && c.bits == bits && c.channels == ch;
}

int main() {
    // The SDR as the session builds it in browser mode: DHU template + the ch 16 system-audio sink.
    setenv("NBT_SYSTEM_SINK", "1", 1);
    std::vector<uint8_t> sdr(kDhuSdpProtobuf, kDhuSdpProtobuf + kDhuSdpProtobufLen);
    CHECK(addSystemAudioSink(sdr) == 1);

    // 1. Offer lists, in the order the phone indexes them.
    std::vector<AaAudioConfig> g = sdrAudioConfigs(&sdr[0], sdr.size(), 8);
    CHECK(g.size() == 2);
    CHECK(g.size() == 2 && is(g[0], 16000, 16, 1) && is(g[1], 48000, 16, 1));   // guidance: 16 kHz, then 48 kHz
    std::vector<AaAudioConfig> m = sdrAudioConfigs(&sdr[0], sdr.size(), 9);
    CHECK(m.size() == 1 && is(m[0], 48000, 16, 2));                              // media: 48 kHz stereo only
    std::vector<AaAudioConfig> s = sdrAudioConfigs(&sdr[0], sdr.size(), 16);
    CHECK(s.size() == 1 && is(s[0], 16000, 16, 1));                              // system: 16 kHz mono
    CHECK(sdrAudioConfigs(&sdr[0], sdr.size(), 6).empty());    // video sink: no audio configs
    CHECK(sdrAudioConfigs(&sdr[0], sdr.size(), 99).empty());   // no such channel
    CHECK(sdrAudioConfigs(NULL, 0, 8).empty());

    // 2. START_INDICATION payloads (after the 2-byte message id), as the car logged them.
    uint32_t sess = 0, idx = 0;
    AaAudioConfig c;
    const uint8_t guidance[] = { 0x08, 0x03, 0x10, 0x01 };     // 80 01 | session 3 | config 1 (car 2026-11-01)
    CHECK(resolveAudioStart(guidance, sizeof(guidance), g, &sess, &idx, &c));
    CHECK(sess == 3 && idx == 1 && is(c, 48000, 16, 1));
    const uint8_t media[] = { 0x08, 0x00, 0x10, 0x00 };        // 80 01 | session 0 | config 0 (car 2026-11-02)
    CHECK(resolveAudioStart(media, sizeof(media), m, &sess, &idx, &c));
    CHECK(sess == 0 && idx == 0 && is(c, 48000, 16, 2));
    const uint8_t first16k[] = { 0x08, 0x05, 0x10, 0x00 };     // a phone that picks the 16 kHz guidance offer
    CHECK(resolveAudioStart(first16k, sizeof(first16k), g, &sess, &idx, &c) && is(c, 16000, 16, 1));

    // 3. No usable index: false, nothing chosen; the caller falls back to measuring.
    const uint8_t noIndex[] = { 0x08, 0x02 };
    c = AaAudioConfig();
    CHECK(!resolveAudioStart(noIndex, sizeof(noIndex), g, &sess, &idx, &c));
    CHECK(sess == 2 && idx == 0xFFFFFFFFu && c.sample_rate == 0);
    const uint8_t outOfRange[] = { 0x08, 0x01, 0x10, 0x05 };
    CHECK(!resolveAudioStart(outOfRange, sizeof(outOfRange), g, &sess, &idx, &c) && idx == 5);
    CHECK(!resolveAudioStart(guidance, sizeof(guidance), std::vector<AaAudioConfig>(), &sess, &idx, &c));
    const uint8_t truncated[] = { 0x08, 0x83 };                // malformed varint: no crash, no result
    CHECK(!resolveAudioStart(truncated, sizeof(truncated), g, &sess, &idx, &c));
    // a bare START_INDICATION (no payload) reports the index as absent, not as 0 (the session logs it that way)
    sess = 7; idx = 0;
    CHECK(!resolveAudioStart(NULL, 0, g, &sess, &idx, &c));
    CHECK(idx == 0xFFFFFFFFu && sess == 0xFFFFFFFFu);

    // 4. Browser mode offers guidance at one rate only (NBT_GUIDANCE_RATE, default 48000).
    {
        setenv("NBT_AUDIO_MODE", "browser", 1);
        unsetenv("NBT_GUIDANCE_RATE");
        std::vector<uint8_t> b = sdr;
        CHECK(limitGuidanceOffers(b) == 1);
        std::vector<AaAudioConfig> g1 = sdrAudioConfigs(&b[0], b.size(), 8);
        CHECK(g1.size() == 1 && is(g1[0], 48000, 16, 1));
        // everything else untouched: the other sinks, and the rest of the SDR byte for byte
        std::vector<AaAudioConfig> m1 = sdrAudioConfigs(&b[0], b.size(), 9);
        CHECK(m1.size() == 1 && is(m1[0], 48000, 16, 2));
        std::vector<AaAudioConfig> s1 = sdrAudioConfigs(&b[0], b.size(), 16);
        CHECK(s1.size() == 1 && is(s1[0], 16000, 16, 1));
        CHECK(b.size() + 9 == sdr.size() || b.size() + 10 == sdr.size() || b.size() + 11 == sdr.size());
        // the phone's only choice is index 0 = 48 kHz
        const uint8_t start0[] = { 0x08, 0x04, 0x10, 0x00 };
        CHECK(resolveAudioStart(start0, sizeof(start0), g1, &sess, &idx, &c) && is(c, 48000, 16, 1));
        // idempotent
        std::vector<uint8_t> b2 = b;
        CHECK(limitGuidanceOffers(b2) == 0 && b2 == b);

        // NBT_GUIDANCE_RATE=16000 keeps the other offer; 0 keeps both; a rate not offered changes nothing
        setenv("NBT_GUIDANCE_RATE", "16000", 1);
        b = sdr;
        CHECK(limitGuidanceOffers(b) == 1);
        g1 = sdrAudioConfigs(&b[0], b.size(), 8);
        CHECK(g1.size() == 1 && is(g1[0], 16000, 16, 1));
        setenv("NBT_GUIDANCE_RATE", "0", 1);
        b = sdr;
        CHECK(limitGuidanceOffers(b) == 0 && b == sdr);
        setenv("NBT_GUIDANCE_RATE", "44100", 1);
        b = sdr;
        CHECK(limitGuidanceOffers(b) == 0 && b == sdr);
        unsetenv("NBT_GUIDANCE_RATE");

        // a sink with one offer is never emptied; other channels and other modes are left alone
        b = sdr;
        CHECK(keepAudioSinkRate(b, 9, 44100) == 0 && b == sdr);
        CHECK(keepAudioSinkRate(b, 9, 48000) == 0 && b == sdr);
        CHECK(keepAudioSinkRate(b, 99, 48000) == 0 && b == sdr);
        setenv("NBT_AUDIO_MODE", "bt", 1);
        CHECK(limitGuidanceOffers(b) == 0 && b == sdr);
        unsetenv("NBT_AUDIO_MODE");
    }

    printf(fails ? "test_audio_format: %d FAILED\n" : "test_audio_format: all passed\n", fails);
    return fails ? 1 : 0;
}
