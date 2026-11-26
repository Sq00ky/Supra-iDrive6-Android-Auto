#!/usr/bin/env python3
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
"""Checks src/screenshot_png.hpp with Python's zlib instead of our own CRC and Adler code: the 300x80 picture from
`test_screenshot_png --png` must decode to the expected rows (filter 0, R,G,B). Argument: that binary."""

import struct
import subprocess
import sys
import zlib

W, H = 300, 80


def expected_rows():
    out = bytearray()
    for y in range(H):
        out.append(0)
        for x in range(W):
            out += bytes(((x * 7 + y * 3) & 0xFF, y, x & 0xFF))
    return bytes(out)


def main():
    data = subprocess.run([sys.argv[1], '--png'], check=True, stdout=subprocess.PIPE).stdout
    assert data[:8] == b'\x89PNG\r\n\x1a\n', 'signature'
    pos, chunks = 8, []
    while pos < len(data):
        n, kind = struct.unpack('>I4s', data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        (crc,) = struct.unpack('>I', data[pos + 8 + n:pos + 12 + n])
        assert zlib.crc32(kind + body) == crc, 'CRC of %r' % kind
        chunks.append((kind, body))
        pos += 12 + n
    assert pos == len(data), 'trailing bytes'
    kinds = [k for k, _ in chunks]
    assert kinds == [b'IHDR', b'IDAT', b'IEND'], kinds
    assert struct.unpack('>IIBBBBB', chunks[0][1]) == (W, H, 8, 2, 0, 0, 0), 'IHDR'
    assert zlib.decompress(chunks[1][1]) == expected_rows(), 'pixels'
    print('test_screenshot_png.py OK')


if __name__ == '__main__':
    main()
