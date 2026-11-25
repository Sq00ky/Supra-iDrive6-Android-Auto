// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#pragma once
// Body of the [RadioText] log line: one per change of a tuner's scrolling text (DAB label, FM radiotext) or DL+/RT+.
#include "radio/most_tuner.hpp"

#include <stdio.h>
#include <string>
#include <vector>

namespace nbt {

// Index 0 DAB, 1 FM: what was logged last.
struct RadioTextLog {
    std::string text[2], plus[2];
};

inline bool radioTextLogBody(RadioTextLog& last, uint8_t fblock, uint16_t fkt, const uint8_t* p, size_t n,
                             std::string& body) {
    const int k = fblock == kMostFBlockDabTuner ? 0 : fblock == kMostFBlockFmTuner ? 1 : -1;
    if (k < 0) return false;
    const char* const name = k == 0 ? "dab" : "fm";
    std::string text;
    const unsigned textFkt = k == 0 ? (unsigned)kMostFktDynamicLabel : (unsigned)kMostFktRadioText;
    if (fkt == textFkt) {
        if (!(k == 0 ? decodeDynamicLabel(p, n, text) : decodeRadioText(p, n, text))) return false;
        if (text == last.text[k]) return false;
        last.text[k] = text;
        body = std::string(name) + " text \"" + text + "\"";
        return true;
    }
    if (fkt != kMostFktDlPlus) return false;
    std::vector<DlPlusEntry> entries;
    if (!decodeDlPlusEntries(p, n, entries)) return false;
    std::string list;
    for (size_t i = 0; i < entries.size(); ++i) {
        char type[16];
        snprintf(type, sizeof(type), " %d=\"", entries[i].type);
        list += type + entries[i].text + "\"";
    }
    if (list.empty()) list = " (none)";
    if (list == last.plus[k]) return false;
    last.plus[k] = list;
    body = std::string(name) + (k == 0 ? " dl+" : " rt+") + list;
    return true;
}

}  // namespace nbt
