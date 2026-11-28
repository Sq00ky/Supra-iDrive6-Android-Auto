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

namespace radio_detail {

// Control characters (DAB line breaks 0x0A/0x0B, soft hyphen 0x1F), '"' and '\' as \xHH: one log line, unambiguous quotes.
inline std::string logEscaped(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        const unsigned char c = (unsigned char)s[i];
        if (c >= 0x20 && c != 0x7F && c != '"' && c != '\\') {
            out += (char)c;
        } else {
            char hex[8];
            snprintf(hex, sizeof(hex), "\\x%02X", c);
            out += hex;
        }
    }
    return out;
}

inline bool logChanged(std::string& last, const std::string& now) {
    if (now == last) return false;
    last = now;
    return true;
}

}  // namespace radio_detail

inline bool radioTextLogBody(RadioTextLog& last, uint8_t fblock, uint16_t fkt, const uint8_t* p, size_t n,
                             std::string& body) {
    const int k = fblock == kMostFBlockDabTuner ? 0 : fblock == kMostFBlockFmTuner ? 1 : -1;
    if (k < 0) return false;
    static const char* const kName[2] = {"dab", "fm"};
    if (fkt == (k == 0 ? (unsigned)kMostFktDynamicLabel : (unsigned)kMostFktRadioText)) {
        std::string text;
        if (!(k == 0 ? decodeDynamicLabel(p, n, text) : decodeRadioText(p, n, text))) return false;
        if (!radio_detail::logChanged(last.text[k], text)) return false;
        body = std::string(kName[k]) + " text \"" + radio_detail::logEscaped(text) + "\"";
        return true;
    }
    if (fkt != kMostFktDlPlus) return false;
    std::vector<DlPlusEntry> entries;
    if (!decodeDlPlusEntries(p, n, entries)) return false;
    std::string list;
    for (size_t i = 0; i < entries.size(); ++i) {
        char type[16];
        snprintf(type, sizeof(type), " %d=\"", entries[i].type);
        list += type + radio_detail::logEscaped(entries[i].text) + "\"";
    }
    if (list.empty()) list = " (none)";
    if (!radio_detail::logChanged(last.plus[k], list)) return false;
    body = std::string(kName[k]) + (k == 0 ? " dl+" : " rt+") + list;
    return true;
}

}  // namespace nbt
