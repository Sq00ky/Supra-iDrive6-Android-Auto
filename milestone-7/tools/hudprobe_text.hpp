// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef HUDPROBE_TEXT_HPP
#define HUDPROBE_TEXT_HPP

#include "cluster/hudterm.hpp"

namespace nbt {
namespace hudprobe {

enum TextEncoding { kUtf16, kLatin9, kUtf8, kTextEncodings };
enum TextKind { kUnknownText, kStreetText, kDistanceText, kUnitText };

inline const char* encodingName(unsigned encoding) {
    switch (encoding) {
        case kUtf16: return "UTF-16BE";
        case kLatin9: return "ISO-8859-15";
        case kUtf8: return "UTF-8";
        default: return "unknown";
    }
}

inline const char* textKindName(unsigned kind) {
    switch (kind) {
        case kStreetText: return "street-like";
        case kDistanceText: return "distance-like";
        case kUnitText: return "unit-like";
        default: return "unclassified";
    }
}

// Visual markers are deliberately ASCII, so the selector is the only variable across tests.
inline bool textProbeMessage(uint16_t host, uint16_t handle, TextEncoding encoding, const char* text,
                             uint32_t tag, most::Msg& out) {
    if (!hudterm::isRingNode(host) || !handle || handle > hudterm::kMaxHandle ||
        encoding < kUtf16 || encoding >= kTextEncodings || strlen(text) > 8) return false;
    for (const char* p = text; *p; ++p) if (*p < 0x20 || *p > 0x7E) return false;
    most::Msg request = most::Msg();
    request.varTag = 1;
    request.dst = host;
    request.fblock = hudterm::kFBlock;
    request.inst = hudterm::kInst;
    request.fkt = hudterm::kTextareaSetText;
    request.opType = most::kOpStartResultAck;
    request.hdrTag = tag;
    if (encoding == kUtf16) request.data = hudterm::setText(handle, text);
    else {
        hudterm::putBe16(request.data, handle);
        request.data.push_back((uint8_t)encoding);
        request.data.insert(request.data.end(), text, text + strlen(text) + 1);
    }
    out = request;
    return true;
}

// Classify public display fields from wire shape without storing a route's text in the report.
inline TextKind factoryTextKind(const uint8_t* data, size_t len) {
    if (!data || len < 4 || data[2] >= kTextEncodings) return kUnknownText;
    std::string word;
    for (size_t i = 3; i < len && word.size() < 48;) {
        unsigned c = data[i++];
        if (data[2] == kUtf16) {
            if (i == len) return kUnknownText;
            c = c * 256 + data[i++];
        }
        if (!c) break;
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        if (c < 0x20) return kUnknownText;
        if (c != ' ') word += c <= 0x7E ? (char)c : '?';
    }
    if (word.empty()) return kUnknownText;
    if (word == "km" || word == "m" || word == "mi" || word == "ft" || word == "yd")
        return kUnitText;
    if (word.find_first_of("0123456789") != std::string::npos &&
        word.find_first_not_of("0123456789.,") == std::string::npos) return kDistanceText;
    return word.find_first_of("abcdefghijklmnopqrstuvwxyz") == std::string::npos ? kUnknownText : kStreetText;
}

} // namespace hudprobe
} // namespace nbt

#endif
