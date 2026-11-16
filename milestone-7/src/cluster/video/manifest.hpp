#pragma once

#include "protocol.hpp"
#include <string.h>

// Remove the Desktop Head Unit (DHU) reference manifest's cluster padding so AA uses
// the full negotiated 800x480 frame. Only channel 2's margins and insets change;
// the main display, other channels and unknown fields remain byte-for-byte exact.
namespace aa_cluster {
namespace manifest_detail {

enum WireType { Varint = 0, Fixed64 = 1, LengthDelimited = 2, Fixed32 = 5 };
enum MessageKind {
    DiscoveryResponse,
    ChannelDescription,
    VideoSink,
    VideoConfiguration,
    AdditionalConfiguration,
    DisplayInsets
};

// Protobuf field numbers from the encoders in sdr_builder.cpp. Each nested
// message has its own numbering, so several fields share the same number.
namespace fields {
enum {
    Channels = 1,
    ChannelId = 1,
    MediaSink = 3,
    VideoConfiguration = 4,
    MarginWidth = 3,
    MarginHeight = 4,
    AdditionalConfiguration = 11,
    DisplayInsets = 1,
    InsetTop = 1,
    InsetBottom = 2,
    InsetLeft = 3,
    InsetRight = 4
};
}

enum { ServiceDiscoveryResponseId = 0x0006, MessageHeaderSize = 2, ClusterVideoChannelId = 2 };

struct FieldView {
    unsigned number;
    WireType wire_type;
    // Encoded scalar bytes, or the payload after a length-delimited header.
    size_t value_begin;
    size_t value_end;

    bool is(unsigned field_number, WireType expected_type) const {
        return number == field_number && wire_type == expected_type;
    }
};

inline bool read_varint(const uint8_t* data, size_t size, size_t& cursor, uint64_t& value) {
    value = 0;
    for (unsigned shift = 0; shift <= 63; shift += 7) {
        if (cursor >= size) {
            return false;
        }
        const uint8_t byte = data[cursor++];
        value |= uint64_t(byte & 0x7f) << shift;
        if (!(byte & 0x80)) {
            return true;
        }
    }
    return false;
}

inline bool read_field(const uint8_t* data, size_t size, size_t& cursor, FieldView& field) {
    uint64_t tag;
    if (!read_varint(data, size, cursor, tag) || tag < 8 || tag > 0xffffffffu) {
        return false;
    }
    field.number = unsigned(tag) >> 3;
    field.wire_type = WireType(unsigned(tag) & 7);
    field.value_begin = cursor;

    switch (field.wire_type) {
    case Varint: {
        uint64_t value;
        if (!read_varint(data, size, cursor, value)) {
            return false;
        }
        break;
    }
    case LengthDelimited: {
        uint64_t length;
        if (!read_varint(data, size, cursor, length) || length > size - cursor) {
            return false;
        }
        field.value_begin = cursor;
        cursor += size_t(length);
        break;
    }
    case Fixed64:
    case Fixed32: {
        const size_t width = field.wire_type == Fixed64 ? 8 : 4;
        if (width > size - cursor) {
            return false;
        }
        cursor += width;
        break;
    }
    default:
        return false;
    }
    field.value_end = cursor;
    return true;
}

inline void clear_varint_value(uint8_t* data, const FieldView& field) {
    // Keep the original byte width, including continuation bytes. For example,
    // 400 (90 03) becomes zero (80 00). This is a valid protobuf varint and
    // preserves every enclosing message length and the caller's packet size.
    for (size_t i = field.value_begin; i < field.value_end; ++i) {
        data[i] = i + 1 == field.value_end ? 0x00 : 0x80;
    }
}

// Follow only this schema path; nesting depth is fixed, not input-controlled:
// discovery -> cluster channel -> video sink -> video configuration,
// then video configuration -> additional configuration -> display insets.
inline bool rewrite_message(uint8_t* data, size_t size, MessageKind kind,
                            unsigned& cluster_channel_count) {
    size_t cursor = 0;
    while (cursor < size) {
        FieldView field;
        if (!read_field(data, size, cursor, field)) {
            return false;
        }
        uint8_t* value = data + field.value_begin;
        const size_t value_size = field.value_end - field.value_begin;

        switch (kind) {
        case DiscoveryResponse:
            if (field.is(fields::Channels, LengthDelimited) &&
                aa_cluster::field(value, value_size, fields::ChannelId, 0) ==
                    ClusterVideoChannelId) {
                ++cluster_channel_count;
                if (!rewrite_message(value, value_size, ChannelDescription,
                                     cluster_channel_count)) {
                    return false;
                }
            }
            break;
        case ChannelDescription:
            if (field.is(fields::MediaSink, LengthDelimited) &&
                !rewrite_message(value, value_size, VideoSink, cluster_channel_count)) {
                return false;
            }
            break;
        case VideoSink:
            if (field.is(fields::VideoConfiguration, LengthDelimited) &&
                !rewrite_message(value, value_size, VideoConfiguration, cluster_channel_count)) {
                return false;
            }
            break;
        case VideoConfiguration:
            if (field.is(fields::MarginWidth, Varint) || field.is(fields::MarginHeight, Varint)) {
                clear_varint_value(data, field);
            }
            if (field.is(fields::AdditionalConfiguration, LengthDelimited) &&
                !rewrite_message(value, value_size, AdditionalConfiguration,
                                 cluster_channel_count)) {
                return false;
            }
            break;
        case AdditionalConfiguration:
            if (field.is(fields::DisplayInsets, LengthDelimited) &&
                !rewrite_message(value, value_size, DisplayInsets, cluster_channel_count)) {
                return false;
            }
            break;
        case DisplayInsets:
            if (field.wire_type == Varint && field.number >= fields::InsetTop &&
                field.number <= fields::InsetRight) {
                clear_varint_value(data, field);
            }
            break;
        }
    }
    return true;
}

} // namespace manifest_detail

// Copy a complete ServiceDiscoveryResponse (including its two-byte message ID)
// and clear cluster video padding without changing the message length. Require
// exactly one cluster channel. On failure, discard destination and send the
// original message; destination may contain a partially rewritten copy.
inline bool rewrite_cluster_video_layout(const uint8_t* source, size_t message_size,
                                         uint8_t* destination, size_t capacity) {
    using namespace manifest_detail;
    if (message_size < MessageHeaderSize || message_size > capacity ||
        source[0] != (ServiceDiscoveryResponseId >> 8) ||
        source[1] != (ServiceDiscoveryResponseId & 0xff)) {
        return false;
    }
    memcpy(destination, source, message_size);
    unsigned cluster_channel_count = 0;
    return rewrite_message(destination + MessageHeaderSize, message_size - MessageHeaderSize,
                           DiscoveryResponse, cluster_channel_count) &&
           cluster_channel_count == 1;
}

} // namespace aa_cluster
