// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// radio_text_log: one line body per change of a tuner's scrolling text, radiotext or DL+/RT+.
#include "radio/radio_text_log.hpp"
#include "radio/wall_time.hpp"
#include "radio_fixtures.hpp"

#include <assert.h>

static bool logDab(RadioTextLog& l, uint16_t fkt, const Bytes& m, std::string& body) {
    return radioTextLogBody(l, kMostFBlockDabTuner, fkt, &m[0], m.size(), body);
}

static bool logFm(RadioTextLog& l, uint16_t fkt, const Bytes& m, std::string& body) {
    return radioTextLogBody(l, kMostFBlockFmTuner, fkt, &m[0], m.size(), body);
}

static Bytes plus3() {
    Bytes v(2, 0);
    const uint8_t types[] = {33, 1, 4};
    const char* texts[] = {"Testprogramm", "Testtitel", "Testinterpret"};
    for (int i = 0; i < 3; ++i) {
        v.push_back(types[i]);
        putStr(v, 0x01, "");
        putStr(v, 0x01, "");
        putStr(v, 0x01, texts[i]);
    }
    return v;
}

static void testTextLogged() {
    RadioTextLog l;
    std::string body;
    assert(logDab(l, kMostFktDynamicLabel, label("Testlauftext"), body));
    assert(body == "dab text \"Testlauftext\"");
    assert(!logDab(l, kMostFktDynamicLabel, label("Testlauftext"), body));
}

static void testControlCharsEscaped() {
    RadioTextLog l;
    std::string body;
    assert(logDab(l, kMostFktDynamicLabel, label("Testzeile eins\nTestzeile zwei\x1f"), body));
    assert(body == "dab text \"Testzeile eins\\x0ATestzeile zwei\\x1F\"");
}

static void testQuoteAndBackslashEscaped() {
    RadioTextLog l;
    std::string body;
    assert(logDab(l, kMostFktDynamicLabel, label("Test \"eins\" a\\b"), body));
    assert(body == "dab text \"Test \\x22eins\\x22 a\\x5Cb\"");
}

static void testHmsMsFormat() {
    const WallTime t;
    const std::string s = t.hmsMs;
    assert(s.size() == 12 && s[2] == ':' && s[5] == ':' && s[8] == '.');
    for (size_t i = 0; i < s.size(); ++i)
        if (i != 2 && i != 5 && i != 8) assert(s[i] >= '0' && s[i] <= '9');
}

static void testPlusLogged() {
    RadioTextLog l;
    std::string body;
    assert(logDab(l, kMostFktDlPlus, plus3(), body));
    assert(body == "dab dl+ 33=\"Testprogramm\" 1=\"Testtitel\" 4=\"Testinterpret\"");
    const uint8_t none[] = {0x00, 0x00};
    assert(logDab(l, kMostFktDlPlus, bytes(none), body));
    assert(body == "dab dl+ (none)");
}

static void testRepeatNotLogged() {
    RadioTextLog l;
    std::string body;
    assert(logFm(l, kMostFktDlPlus, plus3(), body));
    assert(body == "fm rt+ 33=\"Testprogramm\" 1=\"Testtitel\" 4=\"Testinterpret\"");
    assert(!logFm(l, kMostFktDlPlus, plus3(), body));
    assert(logFm(l, kMostFktRadioText, radioText("Testlauftext"), body));
    assert(body == "fm text \"Testlauftext\"");
}

static void testTunersSeparate() {
    RadioTextLog l;
    std::string body;
    assert(logDab(l, kMostFktDynamicLabel, label("Testlauftext"), body));
    assert(logFm(l, kMostFktRadioText, radioText("Testlauftext"), body));
}

static void testOtherMessagesIgnored() {
    RadioTextLog l;
    std::string body;
    assert(!logDab(l, kMostFktDabStation, dabStation(0xD001, "TESTFUNK", "Testfunk Radio"), body));
    assert(!radioTextLogBody(l, kMostFBlockConnectionMaster, kMostFktConnectionTable, kTableDab, sizeof(kTableDab), body));
    assert(!logDab(l, kMostFktSlide, slide(5, 0x05), body));
}

int main() {
    testTextLogged();
    testControlCharsEscaped();
    testQuoteAndBackslashEscaped();
    testHmsMsFormat();
    testPlusLogged();
    testRepeatNotLogged();
    testTunersSeparate();
    testOtherMessagesIgnored();
    printf("test_radio_text_log ok\n");
    return 0;
}
