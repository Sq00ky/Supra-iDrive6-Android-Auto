// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// radio_tile: what the media tile shows and when it is sent.
#include "radio/radio_tile.hpp"
#include "radio_fixtures.hpp"

#include <assert.h>

static const char* kDash = " \xe2\x80\x93 ";

static void dabAudible(RadioState& s) {
    tableMsg(s, bytes(kTableDab));
}

static void fmAudible(RadioState& s) {
    tableMsg(s, bytes(kTableFm));
}

static void dabStationShown(RadioState& s, uint32_t nowMs = 0) {
    dabAudible(s);
    dabMsg(s, kMostFktDabStation, dabStation(0xD001, "TESTFUNK", "Testfunk Radio"), nowMs);
}

static void testNothingBeforeContent() {
    RadioState s;
    assert(!radioTileView(s, false, 0).show);
}

static void testSelectedTunerShowsBeforeItSpeaks() {
    RadioState s;
    dabAudible(s);
    RadioTileView v = radioTileView(s, false, 0);
    assert(v.show && v.source == "DAB" && v.song == "DAB");

    RadioState t;
    dabStationShown(t);
    assert(radioTileView(t, false, 10).song == "Testfunk Radio");
    fmAudible(t);
    assert(!t.fm.spoke);
    v = radioTileView(t, false, 20);
    assert(v.show && v.source == "FM" && v.song == "FM" && v.album.empty());
}

static void testBoldIsStation() {
    RadioState s;
    dabStationShown(s);
    dabMsg(s, kMostFktDlPlus, dlPlus("Probe Titel", "Probe Artist"));
    const RadioTileView v = radioTileView(s, false, 10);
    assert(v.show && !v.hold);
    assert(v.song == "Testfunk Radio");
    assert(v.artist == std::string("Probe Artist") + kDash + "Probe Titel");
    assert(v.album == "Testfunk Radio");
    assert(v.source == "DAB");
    assert(v.state == 2);
}

static void testSecondLine() {
    RadioState s;
    dabStationShown(s);
    dabMsg(s, kMostFktDlPlus, dlPlus("Probe Titel", ""));
    assert(radioTileView(s, false, 10).artist == "Probe Titel");
    dabMsg(s, kMostFktDlPlus, dlPlus("", "Probe Artist"));
    assert(radioTileView(s, false, 10).artist == "Probe Artist");
    dabMsg(s, kMostFktDlPlus, dlPlus("", ""));
    dabMsg(s, kMostFktDynamicLabel, label("Lauftext Probe"));
    assert(radioTileView(s, false, 10).artist == "Lauftext Probe");
    dabMsg(s, kMostFktDynamicLabel, label(""));
    assert(radioTileView(s, false, 10).artist == "");
}

static void testHoldWhileTuning() {
    RadioState s;
    dabAudible(s);
    dabMsg(s, kMostFktDabStation, bytes(kDabTuning), 1000);
    RadioTileView v = radioTileView(s, false, 2000);
    assert(!v.show && v.hold && v.holdUntilMs == 4000);
    v = radioTileView(s, false, 3999);
    assert(!v.show && v.hold);
    v = radioTileView(s, false, 4001);
    assert(v.show && !v.hold && v.song == "DAB");

    dabMsg(s, kMostFktDabStation, dabStation(0xD001, "TESTFUNK", "Testfunk Radio"), 1770);
    v = radioTileView(s, false, 1800);
    assert(v.show && !v.hold && v.song == "Testfunk Radio");
}

static void testHoldAcrossClockWrap() {
    RadioState s;
    dabAudible(s);
    const uint32_t start = 0xFFFFFF00u;
    dabMsg(s, kMostFktDabStation, bytes(kDabTuning), start);
    RadioTileView v = radioTileView(s, false, start + 2000);
    assert(!v.show && v.hold && v.holdUntilMs == start + 3000);
    assert(radioTileView(s, false, start + 3001).show);
}

static void testFmTuningHolds() {
    RadioState s;
    fmAudible(s);
    fmMsg(s, kMostFktFmStation, fmStation("TESTFUNK", 100000, 0xD002), 500);
    fmMsg(s, kMostFktFmStation, fmStation("", 101000, 0xD003), 1000);
    assert(s.fm.tuning);
    RadioTileView v = radioTileView(s, false, 2000);
    assert(!v.show && v.hold && v.holdUntilMs == 4000);
}

static void testFallbackNames() {
    RadioState d;
    dabAudible(d);
    dabMsg(d, kMostFktDynamicLabel, label("Lauftext Probe"));
    RadioTileView v = radioTileView(d, false, 10);
    assert(v.show && v.song == "DAB" && v.album.empty() && v.source == "DAB");

    RadioState f;
    fmAudible(f);
    fmMsg(f, kMostFktFmStation, fmStation("", 100000, -1), 1);
    v = radioTileView(f, false, 10);
    assert(v.show && v.song == "FM 100.0" && v.album.empty() && v.source == "FM");
    fmMsg(f, kMostFktFmStation, fmStation("", 99400, -1), 1);
    assert(radioTileView(f, false, 5000).song == "FM 99.4");
    fmMsg(f, kMostFktFmStation, fmStation("", 99460, -1), 1);
    assert(radioTileView(f, false, 5000).song == "FM 99.5");
    fmMsg(f, kMostFktFmStation, fmStation("", 1017, -1), 1);
    assert(radioTileView(f, false, 5000).song == "AM 1017");

    RadioState n;
    fmAudible(n);
    fmMsg(n, kMostFktRadioText, radioText("Lauftext Probe"));
    v = radioTileView(n, false, 10);
    assert(v.show && v.song == "FM" && v.artist == "Lauftext Probe");
}

static void testFmWithName() {
    RadioState s;
    fmAudible(s);
    fmMsg(s, kMostFktFmStation, fmStation("TESTFUNK", 100000, 0xD002));
    const RadioTileView v = radioTileView(s, false, 10);
    assert(v.show && v.song == "TESTFUNK" && v.album == "TESTFUNK" && v.source == "FM");
}

static void testArt() {
    RadioState s;
    dabStationShown(s);
    assert(radioTileView(s, false, 10).art.empty());
    dabMsg(s, kMostFktLogo, bytes(kLogo));
    assert(radioTileView(s, false, 10).art == s.dab.logo);
    dabMsg(s, kMostFktSlideAnnounce, announce(5));
    dabMsg(s, kMostFktSlide, slide(5, 0x05));
    const RadioTileView v = radioTileView(s, false, 10);
    assert(v.art == s.dab.slide && v.art != s.dab.logo);
    assert(radioTileArt(s.dab) == s.dab.slide);
}

static void testState() {
    RadioState s;
    dabStationShown(s);
    assert(s.audible == 1 && radioTileView(s, false, 10).state == 2);
    tableMsg(s, bytes(kTableNoAudio));
    assert(s.audible == 0 && radioTileView(s, true, 10).state == 1);

    RadioState u;
    dabMsg(u, kMostFktDynamicLabel, label("Lauftext Probe"));
    assert(u.audible == -1);
    assert(radioTileView(u, true, 10).state == 1);
    assert(radioTileView(u, false, 10).state == 2);
}

static void testUnknownTunerPrefersDab() {
    RadioState s;
    fmMsg(s, kMostFktFmStation, fmStation("TESTFUNK", 100000, 0xD002));
    assert(s.tuner == kTunerNone);
    RadioTileView v = radioTileView(s, false, 10);
    assert(v.show && v.source == "FM");
    dabMsg(s, kMostFktDynamicLabel, label("Lauftext Probe"));
    v = radioTileView(s, false, 10);
    assert(v.show && v.source == "DAB");
}

static void checkNeverEmpty(const RadioState& s, uint32_t nowMs) {
    const RadioTileView v = radioTileView(s, false, nowMs);
    assert(!v.show || !v.song.empty());
}

static void testNeverEmptySong() {
    RadioState s;
    dabStationShown(s, 0);
    dabMsg(s, kMostFktLogo, bytes(kLogo));
    checkNeverEmpty(s, 100);
    const uint8_t empty2[] = {0x00, 0x00};
    const uint8_t empty3[] = {0x00, 0x00, 0x00};
    dabMsg(s, kMostFktDabStation, bytes(kDabTuning), 1000);
    checkNeverEmpty(s, 1000);
    dabMsg(s, kMostFktLogo, bytes(kNoLogo), 1000);
    checkNeverEmpty(s, 1000);
    dabMsg(s, kMostFktDynamicLabel, label(""), 1000);
    checkNeverEmpty(s, 1000);
    dabMsg(s, kMostFktDlPlus, bytes(empty2), 1000);
    checkNeverEmpty(s, 1000);
    dabMsg(s, kMostFktSlideAnnounce, bytes(empty3), 1000);
    checkNeverEmpty(s, 1000);
    for (uint32_t t = 1000; t <= 5000; t += 250) checkNeverEmpty(s, t);
    dabMsg(s, kMostFktDabStation, dabStation(0xD001, "TESTFUNK", "Testfunk Radio"), 1770);
    checkNeverEmpty(s, 1770);
    dabMsg(s, kMostFktLogo, bytes(kLogo), 1780);
    checkNeverEmpty(s, 1780);
    dabMsg(s, kMostFktDynamicLabel, label("Lauftext Probe"), 1790);
    checkNeverEmpty(s, 1790);
    dabMsg(s, kMostFktDlPlus, dlPlus("Probe Titel", "Probe Artist"), 1800);
    checkNeverEmpty(s, 1800);
    assert(radioTileView(s, false, 1800).show);
}

static void testPacer() {
    RadioTilePacer p;
    assert(!p.due(0) && p.msUntilDue(0) == 0xFFFFFFFFu);
    p.changed(0);
    assert(!p.due(299) && p.due(300));
    assert(p.msUntilDue(100) == 200);
    assert(p.msUntilDue(300) == 0);

    p.sent();
    assert(!p.due(10000) && p.msUntilDue(10000) == 0xFFFFFFFFu);
    for (uint32_t t = 0; t <= 800; t += 200) p.changed(t);
    assert(!p.due(900));
    assert(p.due(1000));
    assert(p.msUntilDue(900) == 100);

    p.sent();
    p.holdUntil(4000);
    assert(!p.due(3999));
    assert(p.msUntilDue(3500) == 500);
    assert(p.due(4000) && p.msUntilDue(4000) == 0);
    p.sent();
    assert(p.msUntilDue(3500) == 0xFFFFFFFFu);
}

static void testPacerHoldAfterSent() {
    RadioTilePacer p;
    p.changed(0);
    p.sent();
    p.holdUntil(4000);
    assert(p.msUntilDue(3500) == 500);
    assert(!p.due(3999) && p.due(4000));
}

static void testPacerWrap() {
    RadioTilePacer p;
    const uint32_t t0 = 0xFFFFFF00u;
    p.changed(t0);
    assert(!p.due(t0 + 299) && p.due(t0 + 300));
    assert(p.msUntilDue(t0 + 100) == 200);
    p.sent();
    p.holdUntil(t0 + 1000);
    assert(!p.due(t0 + 999) && p.due(t0 + 1000));
    p.reset();
    assert(p.msUntilDue(0) == 0xFFFFFFFFu);
}

static void testChanged() {
    RadioState s;
    dabStationShown(s);
    dabMsg(s, kMostFktLogo, bytes(kLogo));
    const RadioTileView v = radioTileView(s, false, 10);
    RadioTileSent last;
    assert(!last.valid && radioTileChanged(last, v));
    radioTileRemember(last, v);
    assert(last.valid && !radioTileChanged(last, v));

    RadioTileView w = v;
    w.art[w.art.size() - 1] ^= 0x01;
    assert(w.art.size() == v.art.size() && radioTileChanged(last, w));
    w = v;
    w.song = "Anderer Funk";
    assert(radioTileChanged(last, w));
    w = v;
    w.artist = "Probe";
    assert(radioTileChanged(last, w));
    w = v;
    w.album = "Probe";
    assert(radioTileChanged(last, w));
    w = v;
    w.source = "FM";
    assert(radioTileChanged(last, w));
    w = v;
    w.state = 1;
    assert(radioTileChanged(last, w));
}

int main() {
    testNothingBeforeContent();
    testSelectedTunerShowsBeforeItSpeaks();
    testBoldIsStation();
    testSecondLine();
    testHoldWhileTuning();
    testHoldAcrossClockWrap();
    testFmTuningHolds();
    testFallbackNames();
    testFmWithName();
    testArt();
    testState();
    testUnknownTunerPrefersDab();
    testNeverEmptySong();
    testPacer();
    testPacerHoldAfterSent();
    testPacerWrap();
    testChanged();
    printf("test_radio_tile ok\n");
    return 0;
}
