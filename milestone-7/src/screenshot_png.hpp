// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef NBT_SCREENSHOT_PNG_HPP
#define NBT_SCREENSHOT_PNG_HPP

// Uncompressed PNG (deflate "stored" blocks, no zlib) for nbt-aad --screenshot.
// rawSize, fileSize, packRows and write expect sizeOk(w, h).

#include <stddef.h>
#include <stdint.h>

namespace nbt {
namespace png {

const int kMaxSide = 4096;

typedef bool (*PutFn)(void* ctx, const uint8_t* p, size_t n);

inline bool sizeOk(int w, int h) { return w >= 1 && h >= 1 && w <= kMaxSide && h <= kMaxSide; }

// PNG rows: filter byte 0, then R,G,B per pixel.
inline uint32_t rawSize(int w, int h) { return (uint32_t)h * (1u + 3u * (uint32_t)w); }

inline uint32_t crc32(uint32_t crc, const uint8_t* p, size_t n) {
    static uint32_t table[256];
    static bool ready = false;
    if (!ready) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        ready = true;
    }
    crc = ~crc;
    for (size_t i = 0; i < n; ++i) crc = table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

inline uint32_t adler32(uint32_t a, const uint8_t* p, size_t n) {
    uint32_t s1 = a & 0xFFFF, s2 = a >> 16;
    while (n > 0) {
        size_t k = n < 5552 ? n : 5552;  // largest run before s2 can overflow
        n -= k;
        while (k--) {
            s1 += *p++;
            s2 += s1;
        }
        s1 %= 65521;
        s2 %= 65521;
    }
    return (s2 << 16) | s1;
}

// src: B,G,R,X pixels from the top row, rows `stride` bytes apart. out holds rawSize(w, h).
inline void packRows(const uint8_t* src, int w, int h, int stride, uint8_t* out) {
    for (int y = 0; y < h; ++y) {
        const uint8_t* s = src + (size_t)y * (size_t)stride;
        *out++ = 0;
        for (int x = 0; x < w; ++x, s += 4) {
            *out++ = s[2];
            *out++ = s[1];
            *out++ = s[0];
        }
    }
}

namespace detail {

const uint32_t kMaxBlock = 65535;

inline uint32_t zlibSize(uint32_t raw) { return 2 + raw + 5 * ((raw + kMaxBlock - 1) / kMaxBlock) + 4; }

inline void putBe32(uint8_t* b, uint32_t v) {
    b[0] = (uint8_t)(v >> 24);
    b[1] = (uint8_t)(v >> 16);
    b[2] = (uint8_t)(v >> 8);
    b[3] = (uint8_t)v;
}

// Stops calling put after its first failure.
struct ChunkWriter {
    PutFn put;
    void* ctx;
    uint32_t crc;
    bool ok;
    ChunkWriter(PutFn p, void* c) : put(p), ctx(c), crc(0), ok(true) {}
    void raw(const uint8_t* p, size_t n) {
        if (ok && !put(ctx, p, n)) ok = false;
    }
    void data(const uint8_t* p, size_t n) {
        crc = crc32(crc, p, n);
        raw(p, n);
    }
    void begin(uint32_t len, const char* type) {
        uint8_t b[4];
        putBe32(b, len);
        raw(b, 4);
        crc = 0;
        data((const uint8_t*)type, 4);
    }
    void end() {
        uint8_t b[4];
        putBe32(b, crc);
        raw(b, 4);
    }
};

}  // namespace detail

inline uint32_t fileSize(int w, int h) { return 8 + 25 + 12 + detail::zlibSize(rawSize(w, h)) + 12; }

// raw: rawSize(w, h) bytes from packRows. False as soon as put fails.
inline bool write(PutFn put, void* ctx, const uint8_t* raw, int w, int h) {
    using detail::putBe32;
    const uint32_t n = rawSize(w, h);
    detail::ChunkWriter c(put, ctx);
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    c.raw(sig, sizeof sig);

    uint8_t ihdr[13] = {0};
    putBe32(ihdr, (uint32_t)w);
    putBe32(ihdr + 4, (uint32_t)h);
    ihdr[8] = 8;  // bits per channel
    ihdr[9] = 2;  // RGB
    c.begin(sizeof ihdr, "IHDR");
    c.data(ihdr, sizeof ihdr);
    c.end();

    c.begin(detail::zlibSize(n), "IDAT");
    static const uint8_t zhead[2] = {0x78, 0x01};
    c.data(zhead, sizeof zhead);
    uint32_t adler = 1;
    for (uint32_t off = 0; off < n && c.ok;) {
        const uint32_t len = n - off < detail::kMaxBlock ? n - off : detail::kMaxBlock;
        const uint8_t head[5] = {(uint8_t)(off + len == n), (uint8_t)len, (uint8_t)(len >> 8), (uint8_t)~len,
                                 (uint8_t)(~len >> 8)};
        c.data(head, sizeof head);
        c.data(raw + off, len);
        adler = adler32(adler, raw + off, len);
        off += len;
    }
    uint8_t tail[4];
    putBe32(tail, adler);
    c.data(tail, sizeof tail);
    c.end();

    c.begin(0, "IEND");
    c.end();
    return c.ok;
}

}  // namespace png
}  // namespace nbt

#endif
