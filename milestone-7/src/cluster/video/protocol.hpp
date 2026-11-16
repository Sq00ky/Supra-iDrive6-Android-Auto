#pragma once

#include <stdint.h>
#include <stddef.h>

namespace aa_cluster {

enum {
    Port = 34980,
    HeaderSize = 24,
    MaxPayload = 512 * 1024,
    MaxConfig = 64 * 1024,
    QueueSize = 256,
    QueueBytes = 4 * 1024 * 1024
};
enum Kind { Reset = 1, Video = 2, Stop = 3 };

inline uint32_t be32(const uint8_t* bytes) {
    return uint32_t(bytes[0]) << 24 | uint32_t(bytes[1]) << 16 | uint32_t(bytes[2]) << 8 | bytes[3];
}

inline void put32(uint8_t* bytes, uint32_t value) {
    bytes[0] = value >> 24;
    bytes[1] = value >> 16;
    bytes[2] = value >> 8;
    bytes[3] = value;
}

inline uint64_t be64(const uint8_t* bytes) {
    return uint64_t(be32(bytes)) << 32 | be32(bytes + 4);
}

// Loopback packets use a fixed, big-endian header: magic "ACP1", kind, epoch,
// payload length, then the 64-bit presentation timestamp. Reset/Stop have no
// payload. Keep these offsets independent of host structure padding.
inline void header(uint8_t* bytes, unsigned kind, unsigned epoch, unsigned length, uint64_t pts) {
    put32(bytes, 0x41435031);
    put32(bytes + 4, kind);
    put32(bytes + 8, epoch);
    put32(bytes + 12, length);
    put32(bytes + 16, pts >> 32);
    put32(bytes + 20, uint32_t(pts));
}

inline bool valid_header(const uint8_t* bytes) {
    const unsigned kind = be32(bytes + 4);
    const unsigned length = be32(bytes + 12);
    return be32(bytes) == 0x41435031 && kind >= Reset && kind <= Stop &&
           (kind == Video ? (length > 0 && length <= MaxPayload) : length == 0);
}

inline bool annexb_idr(const uint8_t* bytes, size_t size) {
    // A three-byte start code also matches the tail of a four-byte start code.
    for (size_t i = 2; i + 1 < size; ++i) {
        if (bytes[i] == 1 && bytes[i - 1] == 0 && bytes[i - 2] == 0 && (bytes[i + 1] & 31) == 5) {
            return true;
        }
    }
    return false;
}

// Bounded protobuf varint reader for AA control fields (not media bytes).
inline unsigned field(const uint8_t* bytes, size_t size, unsigned wanted, unsigned fallback) {
    size_t cursor = 0;
    while (cursor < size) {
        uint64_t tag = 0;
        uint64_t value = 0;
        unsigned shift = 0;
        do {
            if (cursor == size || shift > 63) {
                return fallback;
            }
            const uint8_t byte = bytes[cursor++];
            tag |= uint64_t(byte & 127) << shift;
            shift += 7;
            if (!(byte & 128)) {
                break;
            }
        } while (true);

        const unsigned wire_type = unsigned(tag) & 7;
        if (wire_type == 0 || wire_type == 2) {
            shift = 0;
            do {
                if (cursor == size || shift > 63) {
                    return fallback;
                }
                const uint8_t byte = bytes[cursor++];
                value |= uint64_t(byte & 127) << shift;
                shift += 7;
                if (!(byte & 128)) {
                    break;
                }
            } while (true);
            if (wire_type == 0 && tag >> 3 == wanted) {
                return value > 0xffffffffu ? fallback : unsigned(value);
            }
            if (wire_type == 2) {
                if (value > size - cursor) {
                    return fallback;
                }
                cursor += size_t(value);
            }
        } else if (wire_type == 1 || wire_type == 5) {
            const size_t skip = wire_type == 1 ? 8 : 4;
            if (skip > size - cursor) {
                return fallback;
            }
            cursor += skip;
        } else {
            return fallback;
        }
    }
    return fallback;
}

} // namespace aa_cluster
