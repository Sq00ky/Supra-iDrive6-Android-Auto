// Presentation changes to copies of the captured navigation graph.
#pragma once
#include "wire.hpp"

namespace kombi { namespace pip { namespace detail {

// Resource backing geometry is independent of DisplaySink's visible PiP area.
struct NativePipResource {
    uint16_t width, height;
    uint8_t color_depth, packaging;
};

// Keep the maneuver geometry/position that selects PiP, but remove the demo
// road/distance text and distance bar. The vendor capture remains untouched.
inline bool clearNavigationLabels(unsigned method, uint8_t* body, unsigned& size) {
    if (size < 9) return true;
    bool road = method == 3 && body[8] == 3;
    bool distance = method == 10 && body[8] == 29;
    if (!road && !distance) return true;
    if (size < 23 || body[21] != body[8] || get32(body + 9) != size - 17) return false;
    unsigned bom = road ? 28 : 44;
    if (size < bom + 4 || get16(body + bom) != 0xfeff) return false;
    unsigned length = get32(body + bom - 4);
    if (length < 4 || (length & 1) || length > size - bom
        || get16(body + bom + length - 2) != 0) return false;
    unsigned tail = bom + length;
    if (distance && tail >= size) return false;
    // The four-byte string consists of its UTF-16BE BOM and terminating NUL.
    put32(body + bom - 4, 4);
    put16(body + bom + 2, 0);
    for (unsigned i = tail; i < size; ++i) body[bom + 4 + i - tail] = body[i];
    size -= length - 4;
    put32(body + 9, size - 17);
    if (distance) {
        body[22] = 255; // Hidden distance bar.
        body[bom + 4] = 255; // No distance unit.
    }
    return true;
}

}}}
