// Presentation changes to copies of the captured navigation graph.
#pragma once
#include "wire.hpp"

namespace kombi {
namespace pip {
namespace detail {

// Resource backing geometry is independent of DisplaySink's visible PiP area.
struct NativePipResource {
    uint16_t width, height;
    // Captured native configuration enums; preserve values without inferring
    // a pixel layout from their numeric representation.
    uint8_t color_depth, packaging;
};

// Keep the maneuver geometry/position that selects PiP, but remove the demo
// road/distance text and distance bar. The vendor capture remains untouched.
inline bool clearNavigationLabels(unsigned method, uint8_t* body, unsigned& size) {
    if (size < 9) {
        return true;
    }
    // Only these captured method/variant pairs contain the labels we remove.
    bool is_road_label = method == 3 && body[8] == 3;
    bool is_distance_label = method == 10 && body[8] == 29;
    if (!is_road_label && !is_distance_label) {
        return true;
    }

    // The serialized variant repeats its tag at byte 21. Its length at byte 9
    // excludes the 17-byte controller/resource/variant prefix.
    if (size < 23 || body[21] != body[8] || get32(body + 9) != size - 17) {
        return false;
    }
    unsigned bom_offset = is_road_label ? 28 : 44;
    if (size < bom_offset + 4 || get16(body + bom_offset) != 0xfeff) {
        return false;
    }
    unsigned string_bytes = get32(body + bom_offset - 4);
    if (string_bytes < 4 || (string_bytes & 1) || string_bytes > size - bom_offset ||
        get16(body + bom_offset + string_bytes - 2) != 0) {
        return false;
    }
    unsigned tail_offset = bom_offset + string_bytes;
    if (is_distance_label && tail_offset >= size) {
        return false;
    }

    // The four-byte string consists of its UTF-16BE BOM and terminating NUL.
    // Move the trailing fields forward, then update both serialized lengths.
    put32(body + bom_offset - 4, 4);
    put16(body + bom_offset + 2, 0);
    for (unsigned i = tail_offset; i < size; ++i) {
        body[bom_offset + 4 + i - tail_offset] = body[i];
    }
    size -= string_bytes - 4;
    put32(body + 9, size - 17);
    if (is_distance_label) {
        body[22] = 255;             // Hidden distance bar.
        body[bom_offset + 4] = 255; // No distance unit.
    }
    return true;
}

} // namespace detail
} // namespace pip
} // namespace kombi
