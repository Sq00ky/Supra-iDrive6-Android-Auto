#pragma once
#include <stdint.h>

namespace kombi {
namespace pip {
namespace detail {

// Legacy simulator APIX capture transport. A 1152-pixel 24-bit CSI scanline
// has the same byte width as 864 BGRA pixels (3456 bytes). Native Screen uses
// its profile-specific factory backing and does not send this APX1 header.
enum {
    Width = 864,
    CaptureWidth = 1152,
    Height = 480,
    Channels = 4,
    FrameBytes = Width * Height * Channels,
    HeaderBytes = 32,
    Port = 41418
};

// All protocol integers below use network byte order.
inline void put16(uint8_t* destination, uint16_t value) {
    destination[0] = value >> 8;
    destination[1] = value;
}

inline void put32(uint8_t* destination, uint32_t value) {
    put16(destination, value >> 16);
    put16(destination + 2, value);
}

inline uint16_t get16(const uint8_t* source) {
    return (uint16_t(source[0]) << 8) | source[1];
}

inline uint32_t get32(const uint8_t* source) {
    return (uint32_t(get16(source)) << 16) | get16(source + 2);
}

// APX1 layout: magic, sequence, CSI dimensions, stride, payload bytes, format.
// The remaining eight bytes are reserved and remain zero.
inline void header(uint8_t* packet, uint32_t sequence) {
    for (unsigned i = 0; i < HeaderBytes; ++i) {
        packet[i] = 0;
    }
    packet[0] = 'A';
    packet[1] = 'P';
    packet[2] = 'X';
    packet[3] = '1';
    put32(packet + 4, sequence);
    put16(packet + 8, CaptureWidth);
    put16(packet + 10, Height);
    put32(packet + 12, Width * Channels);
    put32(packet + 16, FrameBytes);
    put32(packet + 20, 1); // Format 1: raw 24-bit CSI; GSS interprets it as BGRA8888.
}

// Legacy first-row metadata; the factory Screen producer uses its separately
// captured 84-byte profile from native_metadata.hpp.
inline void metadata(uint8_t* frame, uint32_t sequence) {
    for (unsigned i = 0; i < 30; ++i) {
        frame[i] = 0;
    }
    put16(frame, uint16_t(sequence));
    frame[4] = frame[7] = 1;
    put16(frame + 24, uint16_t(sequence));
}

} // namespace detail
} // namespace pip
} // namespace kombi
