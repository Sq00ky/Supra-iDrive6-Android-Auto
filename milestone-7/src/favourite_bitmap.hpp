// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef NBT_FAVOURITE_BITMAP_HPP
#define NBT_FAVOURITE_BITMAP_HPP

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <vector>

namespace nbt {

inline uint32_t bmp32(const unsigned char* bytes) {
    return static_cast<uint32_t>(bytes[0]) |
           (static_cast<uint32_t>(bytes[1]) << 8) |
           (static_cast<uint32_t>(bytes[2]) << 16) |
           (static_cast<uint32_t>(bytes[3]) << 24);
}

// Decode the native BMP's top bar; leave the other output rows transparent.
// Keep the signature and pixel layout unchanged for existing callers.
inline bool favouriteBitmap(FILE* file, uint32_t* destination,
                           int width, int height, int barHeight,
                           int stride, int outputRows = 0) {
    if (!file || !destination || width <= 0 || height <= 0 ||
        width > 4096 || height > 2048 || stride < width ||
        barHeight <= 0 || barHeight > height) {
        return false;
    }

    if (outputRows == 0) {
        outputRows = height;
    }
    if (outputRows < barHeight || outputRows > height) {
        return false;
    }

    unsigned char header[54];
    if (fread(header, 1, sizeof(header), file) != sizeof(header) ||
        header[0] != 'B' || header[1] != 'M' ||
        bmp32(header + 14) < 40) {
        return false;
    }

    const int bmpWidth = static_cast<int32_t>(bmp32(header + 18));
    const int bmpHeight = static_cast<int32_t>(bmp32(header + 22));
    const int bitsPerPixel = header[28] | (header[29] << 8);
    const uint32_t pixelOffset = bmp32(header + 10);

    if (bmpWidth != width ||
        (bmpHeight != height && bmpHeight != -height) ||
        (bitsPerPixel != 24 && bitsPerPixel != 32) ||
        header[26] != 1 || header[27] != 0 ||
        bmp32(header + 30) != 0 ||
        pixelOffset < sizeof(header) || pixelOffset > 4096) {
        return false;
    }

    const int bytesPerPixel = bitsPerPixel / 8;
    const int rowBytes = ((width * bytesPerPixel + 3) / 4) * 4;
    std::vector<unsigned char> row(rowBytes);

    for (int y = 0; y < outputRows; ++y) {
        memset(destination + static_cast<size_t>(y) * stride,
               0, static_cast<size_t>(width) * sizeof(uint32_t));
    }

    for (int y = 0; y < barHeight; ++y) {
        const int sourceRow = bmpHeight < 0 ? y : height - 1 - y;
        const long position = static_cast<long>(pixelOffset) +
                              static_cast<long>(sourceRow) * rowBytes;

        if (fseek(file, position, SEEK_SET) != 0 ||
            fread(&row[0], 1, rowBytes, file) !=
                static_cast<size_t>(rowBytes)) {
            return false;
        }

        for (int x = 0; x < width; ++x) {
            const unsigned char* pixel = &row[x * bytesPerPixel];
            destination[static_cast<size_t>(y) * stride + x] =
                0xff000000u |
                (static_cast<uint32_t>(pixel[0]) << 16) |
                (static_cast<uint32_t>(pixel[1]) << 8) |
                static_cast<uint32_t>(pixel[2]);
        }
    }
    return true;
}

} // namespace nbt

#endif // NBT_FAVOURITE_BITMAP_HPP
