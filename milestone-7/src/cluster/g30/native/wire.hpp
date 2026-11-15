#pragma once
#include <stdint.h>
namespace kombi { namespace pip { namespace detail {
enum { Width = 864, CaptureWidth = 1152, Height = 480, Channels = 4, FrameBytes = Width * Height * Channels,
       HeaderBytes = 32, Port = 41418 };
inline void put16(uint8_t* p, uint16_t n) { p[0] = n >> 8; p[1] = n; }
inline void put32(uint8_t* p, uint32_t n) { put16(p, n >> 16); put16(p + 2, n); }
inline uint16_t get16(const uint8_t* p) { return (uint16_t(p[0]) << 8) | p[1]; }
inline uint32_t get32(const uint8_t* p) { return (uint32_t(get16(p)) << 16) | get16(p + 2); }
inline void header(uint8_t* p, uint32_t sequence) {
    for (unsigned i = 0; i < HeaderBytes; ++i) p[i] = 0;
    p[0] = 'A'; p[1] = 'P'; p[2] = 'X'; p[3] = '1';
    put32(p + 4, sequence); put16(p + 8, CaptureWidth); put16(p + 10, Height);
    put32(p + 12, Width * Channels); put32(p + 16, FrameBytes);
    put32(p + 20, 1); // Format 1: raw 24-bit CSI scanlines. GSS interprets these as BGRA8888.
}
inline void metadata(uint8_t* frame, uint32_t sequence) {
    for (unsigned i = 0; i < 30; ++i) frame[i] = 0;
    put16(frame, uint16_t(sequence)); frame[4] = frame[7] = 1;
    put16(frame + 24, uint16_t(sequence));
}
}}}
