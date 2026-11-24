// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// radio_state: per-tuner radio state from DAB tuner, FM tuner and connection master messages.
#include "radio/radio_state.hpp"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <vector>

using namespace nbt;

typedef std::vector<uint8_t> Bytes;

template <size_t N>
static Bytes bytes(const uint8_t (&a)[N]) {
    return Bytes(a, a + N);
}

static void put(Bytes& v, const char* s) {
    v.insert(v.end(), (const uint8_t*)s, (const uint8_t*)s + strlen(s));
}

static void putStr(Bytes& v, uint8_t cs, const char* s) {
    v.push_back(cs);
    put(v, s);
    v.push_back(0);
}

static void putU32(Bytes& v, uint32_t x) {
    v.push_back((uint8_t)(x >> 24));
    v.push_back((uint8_t)(x >> 16));
    v.push_back((uint8_t)(x >> 8));
    v.push_back((uint8_t)x);
}

// C13 with a station; unknown4 is the last byte of the group after the SId.
static Bytes dabStation(uint32_t sid, const char* shortName, const char* longName, uint8_t unknown4 = 0) {
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
static Bytes fmStation(const char* ps, uint32_t freqKHz, int pi) {
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

static Bytes label(const char* text) {
    Bytes v(7, 0);
    v[4] = 0x01;
    v.push_back(0x02);
    put(v, text);
    v.push_back(0x00);
    v.push_back(0xff);
    return v;
}

static Bytes radioText(const char* text) {
    Bytes v;
    putStr(v, 0x01, text);
    return v;
}

// C2B as the DAB tuner sends it: title (1) and artist (4), first two fields of each entry empty.
static Bytes dlPlus(const char* title, const char* artist) {
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

static Bytes announce(uint8_t id) {
    const uint8_t a[] = {0x01, id, 0x01};
    return bytes(a);
}

// C43; tag is the last JPEG byte, to tell images apart; copy 0x80 or 0x01 (the tuner sends both).
static Bytes slide(uint8_t id, uint8_t tag, uint8_t copy = 0x80) {
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

static bool dabMsg(RadioState& s, uint16_t fkt, const Bytes& m, uint32_t nowMs = 0) {
    return applyMost(s, kMostFBlockDabTuner, fkt, &m[0], m.size(), nowMs);
}

static bool fmMsg(RadioState& s, uint16_t fkt, const Bytes& m, uint32_t nowMs = 0) {
    return applyMost(s, kMostFBlockFmTuner, fkt, &m[0], m.size(), nowMs);
}

static bool tableMsg(RadioState& s, const Bytes& m) {
    return applyMost(s, kMostFBlockConnectionMaster, kMostFktConnectionTable, &m[0], m.size(), 0);
}

static bool emptyContent(const TunerContent& t) {
    return t.name.empty() && t.title.empty() && t.artist.empty() && t.text.empty() && t.slide.empty() &&
           t.logo.empty() && t.slideId == -1 && t.pendingSlide == -1;
}

// DAB tuner showing announced slide 5, so further slides need an announcement.
static void showSlide5(RadioState& s) {
    dabMsg(s, kMostFktSlideAnnounce, announce(5));
    dabMsg(s, kMostFktSlide, slide(5, 0x05));
    assert(s.dab.slideId == 5 && s.dab.slide.size() == 4 && s.dab.slide[3] == 0x05 && s.slideAnnounceSeen);
}

static void testDefaults() {
    RadioState s;
    assert(s.audible == -1 && s.tuner == kTunerNone && !s.slideAnnounceSeen && s.generation == 0);
    assert(s.dabNames.empty() && dabNameFor(s, 0xD001) == NULL);
    const TunerContent* t[] = {&s.dab, &s.fm};
    for (int i = 0; i < 2; ++i) {
        assert(emptyContent(*t[i]));
        assert(!t[i]->tuning && t[i]->tuneStartMs == 0 && !t[i]->spoke && t[i]->id == 0 && t[i]->freqKHz == 0);
    }
}

static void testDabTuneThenName() {
    RadioState s;
    dabMsg(s, kMostFktDynamicLabel, label("TEST - PROBE XYZ"));
    dabMsg(s, kMostFktLogo, bytes(kLogo));
    assert(dabMsg(s, kMostFktDabStation, bytes(kDabTuning), 1000));
    assert(s.dab.tuning && s.dab.tuneStartMs == 1000 && emptyContent(s.dab) && s.slideAnnounceSeen);
    assert(dabMsg(s, kMostFktDabStation, dabStation(0xD001, "TESTFUNK", "Testfunk Radio"), 1770));
    assert(s.dab.name == "Testfunk Radio" && !s.dab.tuning && s.dab.id == 0xD001);
    const std::string* n = dabNameFor(s, 0xD001);
    assert(n && *n == "Testfunk Radio");
}

static void testRetuneOnlyMovesStart() {
    RadioState s;
    dabMsg(s, kMostFktDabStation, bytes(kDabTuning), 1000);
    const uint32_t g = s.generation;
    assert(!dabMsg(s, kMostFktDabStation, bytes(kDabTuning), 1200));
    assert(s.dab.tuning && s.dab.tuneStartMs == 1200 && s.generation == g);
}

static void testDabShortNameFallback() {
    RadioState s;
    assert(dabMsg(s, kMostFktDabStation, dabStation(0xD001, "TESTFUNK", "")));
    assert(s.dab.name == "TESTFUNK");
    const std::string* n = dabNameFor(s, 0xD001);
    assert(n && *n == "TESTFUNK");
}

static void testSameSidKeepsContent() {
    RadioState s;
    dabMsg(s, kMostFktDabStation, dabStation(0xD001, "TESTFUNK", "Testfunk Radio"));
    dabMsg(s, kMostFktDynamicLabel, label("TEST - PROBE XYZ"));
    const uint32_t g = s.generation;
    assert(!dabMsg(s, kMostFktDabStation, dabStation(0xD001, "TESTFUNK", "Testfunk Radio", 0x04)));
    assert(s.dab.text == "TEST - PROBE XYZ" && s.dab.name == "Testfunk Radio" && s.generation == g);
}

static void testFirstStationKeepsContent() {
    RadioState s;
    dabMsg(s, kMostFktDynamicLabel, label("TEST - PROBE XYZ"));
    dabMsg(s, kMostFktSlide, slide(5, 0x05));
    assert(dabMsg(s, kMostFktDabStation, dabStation(0xD001, "TESTFUNK", "Testfunk Radio"), 500));
    assert(s.dab.name == "Testfunk Radio" && s.dab.id == 0xD001 && !s.dab.tuning);
    assert(s.dab.text == "TEST - PROBE XYZ" && s.dab.slideId == 5 && s.dab.slide.size() == 4);
    assert(!s.slideAnnounceSeen);
}

static void testOtherSidIsStationChange() {
    RadioState s;
    dabMsg(s, kMostFktDabStation, dabStation(0xD001, "TESTFUNK", "Testfunk Radio"));
    dabMsg(s, kMostFktDynamicLabel, label("TEST - PROBE XYZ"));
    dabMsg(s, kMostFktDlPlus, dlPlus("Probe Titel", "Probe Artist"));
    dabMsg(s, kMostFktLogo, bytes(kLogo));
    dabMsg(s, kMostFktSlide, slide(5, 0x05));
    assert(!s.slideAnnounceSeen);
    assert(dabMsg(s, kMostFktDabStation, dabStation(0xD002, "TESTFUNK", ""), 5000));
    assert(s.dab.name == "TESTFUNK" && s.dab.id == 0xD002 && !s.dab.tuning && s.slideAnnounceSeen);
    assert(s.dab.text.empty() && s.dab.title.empty() && s.dab.artist.empty());
    assert(s.dab.logo.empty() && s.dab.slide.empty() && s.dab.slideId == -1);
    assert(*dabNameFor(s, 0xD001) == "Testfunk Radio" && *dabNameFor(s, 0xD002) == "TESTFUNK");
}

static void testDabNamesBounded() {
    RadioState s;
    for (uint32_t sid = 1; sid <= 33; ++sid) dabMsg(s, kMostFktDabStation, dabStation(sid, "TESTFUNK", "Testfunk Radio"));
    assert(s.dabNames.size() == 32 && s.dabNames.back().first == 33);
    assert(dabNameFor(s, 1) == NULL && dabNameFor(s, 2) != NULL && dabNameFor(s, 33) != NULL);
    dabMsg(s, kMostFktDabStation, dabStation(2, "TESTFUNK", ""));
    assert(s.dabNames.size() == 32 && s.dabNames.front().first == 3);
    assert(s.dabNames.back().first == 2 && s.dabNames.back().second == "TESTFUNK");
}

static void testDabArrivesBeforeConnectionTable() {
    RadioState s;
    tableMsg(s, bytes(kTableFm));
    assert(s.tuner == kTunerFm);
    dabMsg(s, kMostFktDabStation, dabStation(0xD001, "TESTFUNK", "Testfunk Radio"));
    dabMsg(s, kMostFktLogo, bytes(kLogo));
    assert(s.tuner == kTunerFm && s.dab.name == "Testfunk Radio" && s.dab.logo.size() == 6);
    assert(tableMsg(s, bytes(kTableDab)));
    assert(s.tuner == kTunerDab && s.audible == 1);
    assert(s.dab.name == "Testfunk Radio" && s.dab.logo.size() == 6);
}

static void testFmDoesNotTouchDab() {
    RadioState s;
    tableMsg(s, bytes(kTableDab));
    dabMsg(s, kMostFktDabStation, dabStation(0xD001, "TESTFUNK", "Testfunk Radio"));
    dabMsg(s, kMostFktDynamicLabel, label("TEST - PROBE XYZ"));
    dabMsg(s, kMostFktDlPlus, dlPlus("Probe Titel", "Probe Artist"));
    const TunerContent before = s.dab;
    assert(fmMsg(s, kMostFktFmStation, fmStation("TESTFUNK", 100000, 0xD002)));
    assert(fmMsg(s, kMostFktRadioText, radioText("Radiotext Probe")));
    assert(s.fm.name == "TESTFUNK" && s.fm.id == 0xD002 && s.fm.freqKHz == 100000 && s.fm.text == "Radiotext Probe");
    assert(s.dab.name == before.name && s.dab.text == before.text && s.dab.title == before.title &&
           s.dab.artist == before.artist && s.dab.id == before.id);
    assert(s.tuner == kTunerDab);
}

static void testFmRtPlus() {
    RadioState s;
    assert(fmMsg(s, kMostFktDlPlus, dlPlus("Probe Titel", "Probe Artist")));
    assert(s.fm.title == "Probe Titel" && s.fm.artist == "Probe Artist" && s.dab.title.empty());
    assert(!fmMsg(s, kMostFktDlPlus, dlPlus("Probe Titel", "Probe Artist")));
}

static void testAnnounceKeepsSlide() {
    RadioState s;
    showSlide5(s);
    const uint32_t g = s.generation;
    assert(!dabMsg(s, kMostFktSlideAnnounce, announce(6)));
    assert(s.dab.slideId == 5 && s.dab.slide[3] == 0x05 && s.dab.pendingSlide == 6 && s.generation == g);
    assert(dabMsg(s, kMostFktSlide, slide(6, 0x06)));
    assert(s.dab.slideId == 6 && s.dab.slide[3] == 0x06 && s.dab.pendingSlide == -1);
}

static void testUnannouncedSlideAtStart() {
    RadioState s;
    assert(dabMsg(s, kMostFktSlide, slide(5, 0x05)));
    assert(s.dab.slideId == 5 && s.dab.slide.size() == 4 && s.dab.slide[0] == 0xff);
}

static void testUnannouncedSlideAfterTune() {
    RadioState s;
    dabMsg(s, kMostFktDabStation, bytes(kDabTuning), 1000);
    assert(!dabMsg(s, kMostFktSlide, slide(5, 0x05)));
    assert(s.dab.slide.empty() && s.dab.slideId == -1);
    assert(!dabMsg(s, kMostFktSlideAnnounce, announce(7)));
    assert(dabMsg(s, kMostFktSlide, slide(7, 0x07)));
    assert(s.dab.slideId == 7 && s.dab.slide[3] == 0x07);
}

static void testNoSlideshowCountsAsAnnouncement() {
    RadioState s;
    assert(dabMsg(s, kMostFktSlideAnnounce, bytes(kNoSlideshow)));
    assert(s.slideAnnounceSeen && s.generation == 1);
    assert(!dabMsg(s, kMostFktSlide, slide(5, 0x05)));
    assert(s.dab.slide.empty());
}

static void testUnannouncedSlideIgnoredOnceAnnouncing() {
    RadioState s;
    showSlide5(s);
    dabMsg(s, kMostFktSlideAnnounce, announce(6));
    assert(!dabMsg(s, kMostFktSlide, slide(9, 0x09)));
    assert(s.dab.slideId == 5 && s.dab.slide[3] == 0x05 && s.dab.pendingSlide == 6);
}

static void testNoSlideshowClears() {
    RadioState s;
    showSlide5(s);
    dabMsg(s, kMostFktLogo, bytes(kLogo));
    dabMsg(s, kMostFktDynamicLabel, label("BE"));
    const uint32_t g = s.generation;
    const uint8_t errorAck[] = {0x00, 0x80, 0x43};
    assert(!dabMsg(s, kMostFktSlideAnnounce, bytes(errorAck)));
    assert(s.dab.slideId == 5 && s.dab.slide.size() == 4 && s.generation == g);
    assert(dabMsg(s, kMostFktSlideAnnounce, bytes(kNoSlideshow)));
    assert(s.dab.slide.empty() && s.dab.slideId == -1 && s.dab.pendingSlide == -1 && s.generation == g + 1);
    assert(s.dab.logo.size() == 6 && s.dab.text == "BE");
    assert(!dabMsg(s, kMostFktSlideAnnounce, bytes(kNoSlideshow)));

    // Announced, no slide yet: "no slideshow" drops the number unseen, the same number can come again.
    dabMsg(s, kMostFktSlideAnnounce, announce(0x5c));
    assert(s.dab.pendingSlide == 0x5c);
    assert(!dabMsg(s, kMostFktSlideAnnounce, bytes(kNoSlideshow)));
    assert(s.dab.pendingSlide == -1);
    dabMsg(s, kMostFktSlideAnnounce, announce(0x5c));
    assert(dabMsg(s, kMostFktSlide, slide(0x5c, 0x5c)));
    assert(s.dab.slideId == 0x5c);
}

static void testFmAltFrequencySamePi() {
    RadioState s;
    fmMsg(s, kMostFktFmStation, fmStation("TESTFUNK", 100000, 0xD001));
    fmMsg(s, kMostFktRadioText, radioText("Radiotext Probe"));
    const uint32_t g = s.generation;
    assert(!fmMsg(s, kMostFktFmStation, fmStation("IM RADIO", 101000, 0xD001)));
    assert(s.fm.name == "TESTFUNK" && s.fm.text == "Radiotext Probe" && s.fm.freqKHz == 101000 && s.generation == g);
}

static void testFmNewPiIsStationChange() {
    RadioState s;
    fmMsg(s, kMostFktFmStation, fmStation("TESTFUNK", 100000, 0xD001));
    fmMsg(s, kMostFktRadioText, radioText("Radiotext Probe"));
    fmMsg(s, kMostFktDlPlus, dlPlus("Probe Titel", "Probe Artist"));
    assert(fmMsg(s, kMostFktFmStation, fmStation("TESTZWEI", 101000, 0xD002), 3000));
    assert(s.fm.name == "TESTZWEI" && s.fm.id == 0xD002 && s.fm.freqKHz == 101000 && !s.fm.tuning);
    assert(s.fm.text.empty() && s.fm.title.empty() && s.fm.artist.empty());
}

static void testFmWithoutPiFrequencyIsStation() {
    RadioState s;
    fmMsg(s, kMostFktFmStation, fmStation("TESTFUNK", 100000, -1));
    assert(s.fm.id == 100000 && s.fm.name == "TESTFUNK");
    fmMsg(s, kMostFktRadioText, radioText("Radiotext Probe"));
    assert(fmMsg(s, kMostFktFmStation, fmStation("TESTZWEI", 101000, -1)));
    assert(s.fm.id == 101000 && s.fm.name == "TESTZWEI" && s.fm.text.empty());
}

static void testFmEmptyPsTunes() {
    RadioState s;
    fmMsg(s, kMostFktFmStation, fmStation("TESTFUNK", 100000, 0xD001));
    fmMsg(s, kMostFktRadioText, radioText("Radiotext Probe"));
    assert(fmMsg(s, kMostFktFmStation, fmStation("", 101000, 0xD002), 2000));
    assert(s.fm.tuning && s.fm.tuneStartMs == 2000 && emptyContent(s.fm) && s.fm.id == 0xD002);
    assert(fmMsg(s, kMostFktFmStation, fmStation("TESTZWEI", 101000, 0xD002), 2500));
    assert(!s.fm.tuning && s.fm.name == "TESTZWEI" && s.fm.tuneStartMs == 2000);
}

static void testFmFrequencyWithoutName() {
    RadioState s;
    assert(fmMsg(s, kMostFktFmStation, fmStation("", 100000, 0xD001)));
    assert(s.fm.spoke && !s.fm.tuning && s.fm.name.empty() && s.fm.freqKHz == 100000);
    assert(fmMsg(s, kMostFktFmStation, fmStation("", 101000, 0xD001)));
    assert(s.fm.freqKHz == 101000);
    assert(!fmMsg(s, kMostFktFmStation, fmStation("", 101000, 0xD001)));
}

static void testFirstFmStationKeepsContent() {
    RadioState s;
    fmMsg(s, kMostFktRadioText, radioText("Radiotext Probe"));
    assert(fmMsg(s, kMostFktFmStation, fmStation("TESTFUNK", 100000, 0xD001)));
    assert(s.fm.name == "TESTFUNK" && s.fm.id == 0xD001 && s.fm.text == "Radiotext Probe" && !s.fm.tuning);
}

static void testFmUsesDabNameForPi() {
    RadioState s;
    dabMsg(s, kMostFktDabStation, dabStation(0xD001, "TESTFUNK", "Testfunk Radio"));
    assert(fmMsg(s, kMostFktFmStation, fmStation("TESTFUNK", 100000, 0xD001)));
    assert(s.fm.name == "Testfunk Radio");
    assert(!fmMsg(s, kMostFktFmStation, fmStation("IM RADIO", 101000, 0xD001)));
    assert(s.fm.name == "Testfunk Radio");
}

static void testFmLearnsDabNameLater() {
    RadioState s;
    fmMsg(s, kMostFktFmStation, fmStation("TESTFUNK", 100000, 0xD001));
    dabMsg(s, kMostFktDabStation, dabStation(0xD001, "TESTFUNK", "Testfunk Radio"));
    assert(s.fm.name == "TESTFUNK");
    assert(fmMsg(s, kMostFktFmStation, fmStation("IM RADIO", 100000, 0xD001)));
    assert(s.fm.name == "Testfunk Radio");
    assert(!fmMsg(s, kMostFktFmStation, fmStation("TESTFUNK", 100000, 0xD001)));
}

static void testConnectionTable() {
    RadioState s;
    assert(tableMsg(s, bytes(kTableDab)));
    assert(s.audible == 1 && s.tuner == kTunerDab);
    assert(tableMsg(s, bytes(kTableDabDisplaced)));
    assert(s.audible == 0 && s.tuner == kTunerDab);
    assert(!tableMsg(s, bytes(kTableDabCall)));
    assert(s.audible == 0 && s.tuner == kTunerDab);
    assert(tableMsg(s, bytes(kTableDabChime)));
    assert(s.audible == 1 && s.tuner == kTunerDab);
    assert(tableMsg(s, bytes(kTableNoAudio)));
    assert(s.audible == 0 && s.tuner == kTunerDab);
    assert(tableMsg(s, bytes(kTableFm)));
    assert(s.audible == 1 && s.tuner == kTunerFm);
    const uint8_t bad[] = {0x00, 0x00, 0x00, 0x68};
    assert(!tableMsg(s, bytes(bad)));
    assert(s.audible == 1 && s.tuner == kTunerFm);
    assert(!s.dab.spoke && !s.fm.spoke);

    RadioState off;
    assert(tableMsg(off, bytes(kTableDabDisplaced)));
    assert(off.audible == 0 && off.tuner == kTunerNone);
}

static void testDuplicatesAreNoChange() {
    RadioState s;
    assert(tableMsg(s, bytes(kTableDab)));
    uint32_t g = s.generation;
    assert(!tableMsg(s, bytes(kTableDab)) && s.generation == g);
    assert(dabMsg(s, kMostFktSlide, slide(5, 0x05, 0x80)));
    g = s.generation;
    assert(!dabMsg(s, kMostFktSlide, slide(5, 0x05, 0x01)) && s.generation == g);
    assert(dabMsg(s, kMostFktDynamicLabel, label("TEST - PROBE XYZ")));
    g = s.generation;
    assert(!dabMsg(s, kMostFktDynamicLabel, label("TEST - PROBE XYZ")) && s.generation == g);
}

static void testSpoke() {
    RadioState s;
    assert(!s.dab.spoke && !s.fm.spoke);
    assert(dabMsg(s, kMostFktDynamicLabel, label("")));
    assert(s.dab.spoke && !s.fm.spoke && s.generation == 1);
    assert(!dabMsg(s, kMostFktDynamicLabel, label("")));
    assert(fmMsg(s, kMostFktRadioText, radioText("")));
    assert(s.fm.spoke && s.generation == 2);
}

static void testDlPlus() {
    RadioState s;
    dabMsg(s, kMostFktDynamicLabel, label(""));
    assert(dabMsg(s, kMostFktDlPlus, dlPlus("Probe Titel", "Probe Artist")));
    assert(s.dab.title == "Probe Titel" && s.dab.artist == "Probe Artist" && s.generation == 2);
    assert(!dabMsg(s, kMostFktDlPlus, dlPlus("Probe Titel", "Probe Artist")));
    assert(s.generation == 2);
    const uint8_t empty[] = {0x00, 0x00};
    assert(dabMsg(s, kMostFktDlPlus, bytes(empty)));
    assert(s.dab.title.empty() && s.dab.artist.empty());
}

static void testLabel() {
    RadioState s;
    const Bytes l = label("BE");
    assert(dabMsg(s, kMostFktDynamicLabel, l));
    assert(s.dab.text == "BE");
    assert(!dabMsg(s, kMostFktDynamicLabel, l));
    assert(!applyMost(s, kMostFBlockDabTuner, kMostFktDynamicLabel, &l[0], 5, 0));
}

static void testLogo() {
    RadioState s;
    assert(dabMsg(s, kMostFktLogo, bytes(kLogo)));
    assert(s.dab.logo.size() == 6 && s.dab.logo[0] == 0x89);
    assert(!dabMsg(s, kMostFktLogo, bytes(kLogo)));
}

static void testNoLogo() {
    RadioState s;
    dabMsg(s, kMostFktLogo, bytes(kLogo));
    dabMsg(s, kMostFktSlide, slide(5, 0x05));
    const uint32_t g = s.generation;
    assert(dabMsg(s, kMostFktLogo, bytes(kNoLogo)));
    assert(s.dab.logo.empty() && s.dab.slide.size() == 4 && s.generation == g + 1);
    assert(!dabMsg(s, kMostFktLogo, bytes(kNoLogo)));
    assert(s.generation == g + 1);
    assert(dabMsg(s, kMostFktLogo, bytes(kLogo)));
    assert(s.dab.logo.size() == 6);
}

static void testIgnored() {
    RadioState s;
    const Bytes dl = dlPlus("Probe Titel", "Probe Artist");
    assert(!applyMost(s, 0x99, kMostFktDlPlus, &dl[0], dl.size(), 0));
    assert(!dabMsg(s, 0x123, dl));
    assert(!dabMsg(s, kMostFktRadioText, radioText("Radiotext Probe")));
    assert(!fmMsg(s, kMostFktLogo, bytes(kLogo)));
    assert(!fmMsg(s, kMostFktSlide, slide(5, 0x05)));
    assert(!fmMsg(s, kMostFktDynamicLabel, label("BE")));
    assert(!fmMsg(s, kMostFktDabStation, dabStation(0xD001, "TESTFUNK", "Testfunk Radio")));
    assert(!applyMost(s, kMostFBlockConnectionMaster, 0xC04, kTableDab, sizeof(kTableDab), 0));
    Bytes cut = dabStation(0xD001, "TESTFUNK", "Testfunk Radio");
    cut.resize(48);
    assert(!dabMsg(s, kMostFktDabStation, cut));
    assert(!dabMsg(s, kMostFktSlide, Bytes(5, 0)));
    assert(!fmMsg(s, kMostFktFmStation, Bytes(9, 0)));
    assert(s.generation == 0 && !s.dab.spoke && !s.fm.spoke && s.audible == -1);
    assert(emptyContent(s.dab) && emptyContent(s.fm) && s.dabNames.empty());
}

int main() {
    testDefaults();
    testDabTuneThenName();
    testRetuneOnlyMovesStart();
    testDabShortNameFallback();
    testSameSidKeepsContent();
    testFirstStationKeepsContent();
    testOtherSidIsStationChange();
    testDabNamesBounded();
    testDabArrivesBeforeConnectionTable();
    testFmDoesNotTouchDab();
    testFmRtPlus();
    testAnnounceKeepsSlide();
    testUnannouncedSlideAtStart();
    testUnannouncedSlideAfterTune();
    testNoSlideshowCountsAsAnnouncement();
    testUnannouncedSlideIgnoredOnceAnnouncing();
    testNoSlideshowClears();
    testFmAltFrequencySamePi();
    testFmNewPiIsStationChange();
    testFmWithoutPiFrequencyIsStation();
    testFmEmptyPsTunes();
    testFmFrequencyWithoutName();
    testFirstFmStationKeepsContent();
    testFmUsesDabNameForPi();
    testFmLearnsDabNameLater();
    testConnectionTable();
    testDuplicatesAreNoChange();
    testSpoke();
    testDlPlus();
    testLabel();
    testLogo();
    testNoLogo();
    testIgnored();
    printf("test_radio_state ok\n");
    return 0;
}
