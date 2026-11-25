// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef HUDPROBE_VISUAL_HPP
#define HUDPROBE_VISUAL_HPP

#include "cluster/hudterm.hpp"

#include <errno.h>
#include <sstream>
#include <stdlib.h>

namespace nbt {
namespace hudprobe {

struct VisualLayout {
    std::string name;
    uint8_t terminal;
    uint16_t panel, buffer;
    uint16_t swapFrom;
    int16_t left, top, right, bottom;
    uint8_t colour;
};

inline bool parseVisualLayout(const std::string& line, VisualLayout& out) {
    std::istringstream fields(line);
    VisualLayout layout;
    if (!(fields >> layout.name) || layout.name.size() > 40) return false;
    unsigned value[8];
    for (unsigned i = 0; i < 8; ++i) {
        std::string token;
        if (!(fields >> token) || token[0] == '-') return false;
        char* end = NULL;
        errno = 0;
        const unsigned long n = strtoul(token.c_str(), &end, 0);
        if (*end || errno || n > 1023) return false;
        value[i] = (unsigned)n;
    }
    layout.swapFrom = 0;
    std::string extra;
    if (fields >> extra) {
        char* end = NULL;
        errno = 0;
        const unsigned long source = strtoul(extra.c_str(), &end, 0);
        if (!source || *end || errno || source > hudterm::kMaxHandle ||
            source == value[1] || source == value[2] || fields >> extra) return false;
        layout.swapFrom = (uint16_t)source;
    }
    if ((value[0] != 0 && value[0] != 2) || !value[1] || !value[2] ||
        value[1] > hudterm::kMaxHandle || value[2] > hudterm::kMaxHandle || value[1] == value[2] ||
        value[5] < value[3] + 20 || value[6] < value[4] + 20 || value[7] > 31) return false;
    layout.terminal = (uint8_t)value[0];
    layout.panel = (uint16_t)value[1];
    layout.buffer = (uint16_t)value[2];
    layout.left = (int16_t)value[3];
    layout.top = (int16_t)value[4];
    layout.right = (int16_t)value[5];
    layout.bottom = (int16_t)value[6];
    layout.colour = (uint8_t)value[7];
    out = layout;
    return true;
}

inline void addMarkerBar(std::vector<hudterm::Call>& calls, uint16_t buffer, uint16_t item,
                         int16_t l, int16_t t, int16_t r, int16_t b) {
    const hudterm::Point corners[] = {{l, t}, {r, t}, {r, b}, {l, b}};
    const std::vector<hudterm::Point> points(corners, corners + 4);
    calls.push_back(hudterm::Call(hudterm::kAddPolygon, hudterm::addPolygon(buffer, item, hudterm::kPolygon, points)));
}

inline std::vector<hudterm::Call> visualPattern(const VisualLayout& layout) {
    using namespace hudterm;
    std::vector<Call> calls;
    const uint16_t h = layout.buffer;
    const int16_t l = layout.left, t = layout.top, r = layout.right, b = layout.bottom;
    const int16_t x = (int16_t)((l + r) / 2), y = (int16_t)((t + b) / 2);
    calls.push_back(Call(kRemoveAllItems, removeAllItems(h)));
    calls.push_back(Call(kSetLineWidth, setLineWidth(h, 0)));
    calls.push_back(Call(kSetFillColor, setFillColor(h, layout.colour)));
    addMarkerBar(calls, h, 0, l, t, r, t + 2);
    addMarkerBar(calls, h, 1, l, b - 2, r, b);
    addMarkerBar(calls, h, 2, l, t + 2, l + 2, b - 2);
    addMarkerBar(calls, h, 3, r - 2, t + 2, r, b - 2);
    addMarkerBar(calls, h, 4, x - 4, y - 1, x + 4, y + 1);
    addMarkerBar(calls, h, 5, x - 1, y - 4, x + 1, y + 4);
    addMarkerBar(calls, h, 6, l + 5, t + 5, l + 7, t + 7); // distinguishes the top-left corner
    if (layout.swapFrom) calls.push_back(Call(kSwitchVisibility, switchVisibility(layout.swapFrom, h)));
    else calls.push_back(Call(kSetVisibility, setVisibility(h, true)));
    calls.push_back(Call(kSetVisibility, setVisibility(layout.panel, true)));
    return calls;
}

inline std::vector<hudterm::Call> clearVisualPattern(const VisualLayout& layout, bool hidePanel) {
    using namespace hudterm;
    std::vector<Call> calls;
    if (hidePanel) calls.push_back(Call(kSetVisibility, setVisibility(layout.panel, false)));
    if (layout.swapFrom) calls.push_back(Call(kSetVisibility, setVisibility(layout.buffer, false)));
    calls.push_back(Call(kRemoveAllItems, removeAllItems(layout.buffer)));
    return calls;
}

inline bool visualAnswer(const std::string& answer) {
    if (answer == "full" || answer == "none" || answer == "?" || answer == "q") return true;
    return !answer.empty() && answer.size() <= 4 && answer.find_first_not_of("lrtb") == std::string::npos;
}

} // namespace hudprobe
} // namespace nbt

#endif
