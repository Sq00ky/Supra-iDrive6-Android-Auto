// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Host test for src/screenshot_png.hpp. `--png` writes the 300x80 picture test_screenshot_png.py decodes to stdout.

#include "screenshot_png.hpp"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

namespace png = nbt::png;

namespace {

struct Sink {
    std::string out;
    int calls;
    int failAt;  // 0: never
    Sink() : calls(0), failAt(0) {}
};

bool put(void* ctx, const uint8_t* p, size_t n) {
    Sink* s = (Sink*)ctx;
    s->calls++;
    if (s->failAt && s->calls >= s->failAt) return false;
    s->out.append((const char*)p, n);
    return true;
}

bool putStdout(void*, const uint8_t* p, size_t n) { return fwrite(p, 1, n, stdout) == n; }

const uint8_t* u8(const char* s) { return (const uint8_t*)s; }

std::string bytes(const char* p, size_t n) { return std::string(p, n); }

// Big-endian on its own, so the expected bytes don't lean on the header.
void put32(std::string& s, uint32_t v) {
    s += (char)(v >> 24);
    s += (char)(v >> 16);
    s += (char)(v >> 8);
    s += (char)v;
}

void chunk(std::string& s, const char* type, const std::string& data) {
    put32(s, (uint32_t)data.size());
    std::string td = std::string(type, 4) + data;
    s += td;
    put32(s, png::crc32(0, u8(td.data()), td.size()));
}

std::string writeZeros(int w, int h) {
    std::vector<uint8_t> raw(png::rawSize(w, h), 0);
    Sink s;
    assert(png::write(put, &s, &raw[0], w, h));
    assert(s.out.size() == png::fileSize(w, h));
    return s.out;
}

void sizes() {
    assert(png::sizeOk(1, 1) && png::sizeOk(4096, 4096));
    assert(!png::sizeOk(0, 1) && !png::sizeOk(1, 0) && !png::sizeOk(4097, 1) && !png::sizeOk(1, 4097));
    assert(png::rawSize(1280, 480) == 1843680);
    assert(png::fileSize(1280, 480) == 1843888);
    assert(png::fileSize(1, 1) == 72);
    puts("sizes OK");
}

void checksums() {
    assert(png::crc32(0, u8("123456789"), 9) == 0xCBF43926u);
    assert(png::crc32(0, u8("IEND"), 4) == 0xAE426082u);
    assert(png::crc32(png::crc32(0, u8("1234"), 4), u8("56789"), 5) == 0xCBF43926u);
    assert(png::adler32(1, u8("Wikipedia"), 9) == 0x11E60398u);
    assert(png::adler32(png::adler32(1, u8("Wiki"), 4), u8("pedia"), 5) == 0x11E60398u);
    puts("checksums OK");
}

void onePixel() {
    const uint8_t bgrx[4] = {0x10, 0x20, 0x30, 0xFF};
    uint8_t raw[4];
    png::packRows(bgrx, 1, 1, 4, raw);
    Sink s;
    assert(png::write(put, &s, raw, 1, 1));

    const uint8_t data[4] = {0x00, 0x30, 0x20, 0x10};
    std::string want = bytes("\x89PNG\r\n\x1a\n", 8);
    chunk(want, "IHDR", bytes("\0\0\0\1\0\0\0\1\x08\x02\0\0\0", 13));
    std::string idat = bytes("\x78\x01\x01\x04\x00\xFB\xFF", 7) + bytes((const char*)data, 4);
    put32(idat, png::adler32(1, data, 4));
    chunk(want, "IDAT", idat);
    chunk(want, "IEND", "");
    assert(s.out == want);
    puts("one pixel OK");
}

void packRowsDropsPadding() {
    const uint8_t src[24] = {1, 2, 3, 0xAA, 4, 5, 6, 0xAA, 0xEE, 0xEE, 0xEE, 0xEE,
                             7, 8, 9, 0xAA, 10, 11, 12, 0xAA, 0xEE, 0xEE, 0xEE, 0xEE};
    uint8_t out[14];
    memset(out, 0x77, sizeof out);
    assert(png::rawSize(2, 2) == sizeof out);
    png::packRows(src, 2, 2, 12, out);
    const uint8_t want[14] = {0, 3, 2, 1, 6, 5, 4, 0, 9, 8, 7, 12, 11, 10};
    assert(memcmp(out, want, sizeof want) == 0);
    puts("packRows drops padding OK");
}

void blockBoundary() {
    // 1456x15: exactly 65535 raw bytes, one final block.
    assert(png::rawSize(1456, 15) == 65535);
    std::string a = writeZeros(1456, 15);
    assert(a.size() == 65603);
    assert(a.substr(41, 7) == bytes("\x78\x01\x01\xFF\xFF\x00\x00", 7));

    // 5x4096: 65536 raw bytes, a full block and a final one of 1 byte.
    assert(png::rawSize(5, 4096) == 65536);
    std::string b = writeZeros(5, 4096);
    assert(b.size() == 65609);
    assert(b.substr(41, 7) == bytes("\x78\x01\x00\xFF\xFF\x00\x00", 7));
    assert(b.substr(48 + 65535, 5) == bytes("\x01\x01\x00\xFE\xFF", 5));
    puts("block boundary OK");
}

void putFails() {
    uint8_t raw[4] = {0, 1, 2, 3};
    Sink s;
    s.failAt = 3;
    assert(!png::write(put, &s, raw, 1, 1));
    assert(s.calls == 3);
    puts("put fails OK");
}

int writePicture() {
    const int w = 300, h = 80, stride = w * 4 + 12;
    std::vector<uint8_t> src((size_t)stride * h, 0xEE);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            uint8_t* p = &src[(size_t)y * stride + x * 4];
            p[0] = (uint8_t)x;
            p[1] = (uint8_t)y;
            p[2] = (uint8_t)(x * 7 + y * 3);
            p[3] = 0x5A;
        }
    std::vector<uint8_t> raw(png::rawSize(w, h));
    png::packRows(&src[0], w, h, stride, &raw[0]);
    bool ok = png::write(putStdout, NULL, &raw[0], w, h);
    return (fflush(stdout) == 0 && ok) ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc == 2 && !strcmp(argv[1], "--png")) return writePicture();
    sizes();
    checksums();
    onePixel();
    packRowsDropsPadding();
    blockBoundary();
    putFails();
    puts("test_screenshot_png OK");
    return 0;
}
