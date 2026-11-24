// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// most_tuner: decoders for DAB tuner and connection master payloads.
#include "radio/most_tuner.hpp"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <vector>

using namespace nbt;

#define ARR(a) a, sizeof(a)

static void testMostString() {
    const uint8_t latin[] = {0x47, 0xE4, 0x72, 0xA4};
    assert(mostString(0x01, ARR(latin)) == "G\xC3\xA4r\xE2\x82\xAC");
    const uint8_t utf16[] = {0x41, 0x00, 0x42};
    assert(mostString(0x00, ARR(utf16)) == "A?B");
    const uint8_t u8[] = {0x41, 0xC3, 0xA4};
    assert(mostString(0x02, ARR(u8)) == "A\xC3\xA4");
    assert(mostString(0x01, NULL, 0).empty());
}

static void testDlPlus() {
    const uint8_t m[] = {0x00, 0x00, 0x01, 0x01, 0x00, 0x01, 0x00, 0x01, 'P', 'r', 'o', 'b', 'e', ' ', 'T', 'i', 't', 'e',
                         'l', 0x00, 0x04, 0x01, 0x00, 0x01, 0x00, 0x01, 'P', 'r', 'o', 'b', 'e', ' ', 'A', 'r', 't', 'i',
                         's', 't', 0x00};
    std::string t, a;
    assert(decodeDlPlus(ARR(m), t, a));
    assert(t == "Probe Titel" && a == "Probe Artist");
    const uint8_t empty[] = {0x00, 0x00};
    t = "x"; a = "y";
    assert(decodeDlPlus(ARR(empty), t, a) && t.empty() && a.empty());
    assert(!decodeDlPlus(m, 10, t, a));

    // FM tuner: all three fields of an entry are filled.
    const uint8_t fm[] = {0x00, 0x00,
                          0x21, 0x01, 0x3f, 0x3f, 0x3f, 0x00, 0x01, 0x3f, 0x30, 0x00,   // 21 "???" "?0"
                          0x01, 0x50, 0x72, 0x6f, 0x62, 0x65, 0x00,                     //    "Probe"
                          0x01, 0x01, 0x3f, 0x3f, 0x3f, 0x00, 0x01, 0x3f, 0x30, 0x00,   // 1 (title) "???" "?0"
                          0x01, 0x50, 0x72, 0x6f, 0x62, 0x65, 0x00,                     //    "Probe"
                          0x04, 0x01, 0x3f, 0x3f, 0x3f, 0x00, 0x01, 0x3f, 0x31, 0x00,   // 4 (artist) "???" "?1"
                          0x01, 0x54, 0x65, 0x73, 0x74, 0x73, 0x00};                    //    "Tests"
    assert(decodeDlPlus(ARR(fm), t, a));
    assert(t == "Probe" && a == "Tests");
    assert(!decodeDlPlus(fm, 3, t, a));    // type only
    assert(!decodeDlPlus(fm, 8, t, a));    // first field only
    assert(!decodeDlPlus(fm, 16, t, a));   // third field without 0x00
}

static void testDynamicLabel() {
    const uint8_t m[] = {0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x02, 0x54, 0x45, 0x53, 0x54, 0x20, 0x2d, 0x20,
                         0x50, 0x52, 0x4f, 0x42, 0x45, 0x20, 0x58, 0x59, 0x5a, 0x00, 0xff, 0xff, 0x00, 0x00, 0x01, 0x00};
    std::string t;
    assert(decodeDynamicLabel(ARR(m), t) && t == "TEST - PROBE XYZ");
    const uint8_t e[] = {0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x02, 0x00, 0xff, 0xff, 0x00, 0x00, 0x01, 0x00};
    t = "x";
    assert(decodeDynamicLabel(ARR(e), t) && t.empty());
    assert(!decodeDynamicLabel(m, 5, t));
    const uint8_t noTerm[] = {0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x02, 0x41, 0x42};
    assert(!decodeDynamicLabel(ARR(noTerm), t));
    assert(!decodeDynamicLabel(noTerm, 8, t));
}

static void testSlides() {
    const uint8_t ann[] = {0x01, 0x5a, 0x01};
    assert(decodeSlideAnnounce(ARR(ann)) == 0x5a);
    const uint8_t bad[] = {0x00, 0x00, 0x00};
    assert(decodeSlideAnnounce(ARR(bad)) == -1);
    assert(decodeSlideAnnounce(ann, 2) == -1);

    const uint8_t s[] = {0x00, 0x80, 0x5a, 0x01, 0xff, 0xd8, 0xff, 0xe0};
    int id = 0; const uint8_t* img = NULL; size_t len = 0;
    assert(decodeSlide(ARR(s), id, img, len));
    assert(id == 0x5a && img == s + 4 && len == 4);
    const uint8_t nojpg[] = {0x00, 0x80, 0x5a, 0x01, 0x00, 0x00};
    assert(!decodeSlide(ARR(nojpg), id, img, len));
    assert(!decodeSlide(s, 5, id, img, len));
}

static void testLogo() {
    const uint8_t l[] = {0x01, 0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a};
    const uint8_t* img = NULL; size_t len = 0;
    assert(decodeLogo(ARR(l), img, len) && img == l + 1 && len == 6);
    const uint8_t one[] = {0x01};
    assert(!decodeLogo(ARR(one), img, len));
    assert(!decodeLogo(l, 4, img, len));
}

static void put(std::vector<uint8_t>& v, const char* s) {
    v.insert(v.end(), (const uint8_t*)s, (const uint8_t*)s + strlen(s));
}

static void putStr(std::vector<uint8_t>& v, uint8_t cs, const char* s) {
    v.push_back(cs);
    put(v, s);
    v.push_back(0);
}

static std::vector<uint8_t> dabFilled(uint8_t csLong, const char* longName) {
    std::vector<uint8_t> v(2, 0);
    putStr(v, 0x02, "Testensemble");
    putStr(v, 0x02, "Testensemble");
    const uint8_t eid[] = {0x10, 0x01, 0xe0};
    v.insert(v.end(), eid, eid + 3);
    putStr(v, 0x02, "TESTFUNK");
    putStr(v, csLong, longName);
    const uint8_t tail[] = {0x00, 0x00, 0xd0, 0x01, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00};
    v.insert(v.end(), tail, tail + sizeof(tail));
    putStr(v, 0x02, "TESTFUNK");
    putStr(v, 0x02, "Testfunk Radio");
    v.insert(v.end(), 4, 0);
    return v;
}

static void testDabStationFilled() {
    const std::vector<uint8_t> v = dabFilled(0x02, "Testfunk Radio");
    DabStation d;
    assert(decodeDabStation(&v[0], v.size(), d));
    assert(d.shortName == "TESTFUNK" && d.longName == "Testfunk Radio");
    assert(d.sid == 0xD001 && !d.tuning);
}

static void testDabStationTuning() {
    const uint8_t m[] = {0x00, 0x00, 0x02, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00,
                         0x00, 0x00, 0x00, 0x00, 0x00, 0x90, 0x00, 0x02, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00};
    DabStation d;
    assert(decodeDabStation(ARR(m), d));
    assert(d.tuning && d.shortName.empty() && d.longName.empty() && d.sid == 0);
}

static void testDabStationTruncated() {
    const std::vector<uint8_t> v = dabFilled(0x02, "Testfunk Radio");
    DabStation d;
    assert(!decodeDabStation(&v[0], 48, d));    // inside the long name
    assert(!decodeDabStation(&v[0], 67, d));    // state byte missing
    assert(decodeDabStation(&v[0], 68, d));     // everything after the state byte is ignored
    assert(!decodeDabStation(NULL, 0, d));
}

static void testDabStationLatin9() {
    const std::vector<uint8_t> v = dabFilled(0x01, "\xd6" "ffnung");
    DabStation d;
    assert(decodeDabStation(&v[0], v.size(), d));
    assert(d.longName == "\xc3\x96" "ffnung");
}

static void testFmStation() {
    const uint8_t m[] = {0x01, 'T', 'E', 'S', 'T', 'F', 'U', 'N', 'K', 0x00, 0x01, 0x00, 0x01, 0x86, 0xa0,
                         0x01, 0xd0, 0x01, 0x00, 0x0a, 0xd6, 0x01, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00};
    FmStation f;
    assert(decodeFmStation(ARR(m), f));
    assert(f.ps == "TESTFUNK" && f.freqKHz == 100000 && f.pi == 0xD001);
    uint8_t n[sizeof(m)];
    memcpy(n, m, sizeof(m));
    n[15] = 0x00;
    assert(decodeFmStation(ARR(n), f) && f.pi == -1);
    assert(!decodeFmStation(m, 12, f));
    assert(!decodeFmStation(m, 9, f));
    assert(!decodeFmStation(NULL, 0, f));
}

static void testRadioText() {
    const uint8_t m[] = {0x01, 'R', 'a', 'd', 'i', 'o', 't', 'e', 'x', 't', ' ', 'P', 'r', 'o', 'b', 'e', 0x00};
    std::string t;
    assert(decodeRadioText(ARR(m), t) && t == "Radiotext Probe");
    const uint8_t e[] = {0x01, 0x00};
    t = "x";
    assert(decodeRadioText(ARR(e), t) && t.empty());
    assert(!decodeRadioText(m, 5, t));
    assert(!decodeRadioText(m, 0, t));
}

static void testFmRtPlus() {
    const uint8_t m[] = {0x00, 0x00, 0x01, 0x01, 0x00, 0x01, 0x00, 0x01, 'P', 'r', 'o', 'b', 'e', ' ', 'T', 'i', 't', 'e',
                         'l', 0x00, 0x04, 0x01, 0x00, 0x01, 0x00, 0x01, 'P', 'r', 'o', 'b', 'e', ' ', 'A', 'r', 't', 'i',
                         's', 't', 0x00};
    std::string t, a;
    assert(decodeDlPlus(ARR(m), t, a));
    assert(t == "Probe Titel" && a == "Probe Artist");
}

static void testConnectionTable() {
    const uint8_t a[] = {0x00, 0x00, 0x00, 0x68, 0x04, 0x00, 0xb8, 0x00, 0x01, 0xc1, 0x00};
    assert(audibleTuner(ARR(a)) == 0);
    const uint8_t b[] = {0x00, 0x00, 0x00, 0x68, 0x00, 0x01, 0xc1, 0x00};
    assert(audibleTuner(ARR(b)) == 0x68);
    const uint8_t c[] = {0x00, 0x00, 0x00, 0x58, 0x00, 0x00, 0x68, 0x00, 0x01, 0xc1, 0x00};
    assert(audibleTuner(ARR(c)) == 0x68);
    const uint8_t d[] = {0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0xc1, 0x00};
    assert(audibleTuner(ARR(d)) == 0x01);
    const uint8_t e[] = {0x00, 0x00, 0x00, 0xb8, 0x00, 0x00, 0xc7, 0x04, 0x01, 0xc1, 0x00};
    assert(audibleTuner(ARR(e)) == 0);
    const uint8_t z1[] = {0x00, 0x00, 0x01, 0x68, 0x00};
    assert(audibleTuner(ARR(z1)) == 0);
    const uint8_t noAudio[] = {0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x68, 0x01, 0x01, 0xc1, 0x00};
    assert(audibleTuner(ARR(noAudio)) == 0);
    for (unsigned src = 0x08; src <= 0x0A; ++src) assert(!mostIsTunerSource((uint8_t)src));
    assert(mostIsTunerSource(0x06) && mostIsTunerSource(0x0B) && mostIsTunerSource(0x10));

    const uint8_t trunc[] = {0x00, 0x00, 0x00, 0x68};
    assert(!connectionTableValid(ARR(trunc)));
    assert(connectionTableValid(ARR(b)));
    assert(connectionTableValid(trunc, 2));
    assert(!connectionTableValid(trunc, 1));
}

int main() {
    testMostString();
    testDlPlus();
    testDynamicLabel();
    testSlides();
    testLogo();
    testConnectionTable();
    testDabStationFilled();
    testDabStationTuning();
    testDabStationTruncated();
    testDabStationLatin9();
    testFmStation();
    testRadioText();
    testFmRtPlus();
    printf("test_most_tuner ok\n");
    return 0;
}
