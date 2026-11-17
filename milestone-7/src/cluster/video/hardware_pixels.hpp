#pragma once
#include "cluster/profiles/video.hpp"
#include <stdint.h>
#include <stddef.h>
#include <string.h>

namespace aa_cluster {

// Screen has already scaled the decoded window into FittedWidth x ContentHeight.
// Copy its rows once into the proven PiP content rectangle; padding and alpha
// are ours. No CPU interpolation or second scaling pass is performed here.
inline bool pack_scaled_hardware_picture(const uint8_t* source, unsigned width, unsigned height,
                                         unsigned stride, uint8_t* destination) {
    typedef nbt::cluster::VideoProfile P;
    if (!source || !destination || width != P::FittedWidth || height != P::ContentHeight ||
        stride < width * 4 || stride > 65536) return false;
    for (unsigned y = 0; y < P::ContentHeight; ++y) {
        uint8_t* row = destination + y * P::ContentWidth * 4;
        memset(row, 0, P::ContentWidth * 4);
        memcpy(row + P::PaddingX * 4, source + y * stride, width * 4);
        // Match the previous fit: both image and sidebars are fully opaque,
        // including when Screen returns a source format with a different alpha.
        for (unsigned x = 0; x < P::ContentWidth; ++x) row[x * 4 + 3] = 255;
    }
    return true;
}

// Extract only SPS/PPS Annex-B NALs for the native codec-configuration packet.
// The complete original access unit is still submitted afterwards.
inline unsigned hardware_configuration(const uint8_t* data, unsigned size,
                                       uint8_t* output, unsigned capacity) {
    unsigned used = 0; bool sps = false, pps = false;
    for (unsigned start = 0; start + 3 < size;) {
        unsigned prefix = !data[start] && !data[start + 1] ?
            (data[start + 2] == 1 ? 3 :
             !data[start + 2] && data[start + 3] == 1 ? 4 : 0) : 0;
        if (!prefix || start + prefix >= size) { ++start; continue; }
        unsigned end = start + prefix + 1;
        while (end + 3 < size && !(data[end] == 0 && data[end + 1] == 0 &&
               (data[end + 2] == 1 || (data[end + 2] == 0 && data[end + 3] == 1)))) ++end;
        if (end + 3 >= size) end = size;
        const unsigned kind = data[start + prefix] & 31;
        if (kind == 7 || kind == 8) {
            if (end - start > capacity - used) return 0;
            memcpy(output + used, data + start, end - start); used += end - start;
            sps = sps || kind == 7; pps = pps || kind == 8;
        }
        start = end;
    }
    return sps && pps ? used : 0;
}
} // namespace aa_cluster
