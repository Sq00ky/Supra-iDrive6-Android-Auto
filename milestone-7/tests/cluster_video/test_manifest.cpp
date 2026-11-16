#include "cluster/video/manifest.hpp"
#include "../dhu_sdp.h"

#include <assert.h>
#include <stdio.h>
#include <vector>

using Bytes = std::vector<uint8_t>;
using aa_cluster::rewrite_cluster_video_layout;

static Bytes discovery_response() {
    Bytes message = {0x00, 0x06};
    message.insert(message.end(), kDhuSdpProtobuf, kDhuSdpProtobuf + kDhuSdpProtobufLen);
    return message;
}

static bool rewrite(const Bytes &source, Bytes &destination) {
    destination.resize(source.size());
    return rewrite_cluster_video_layout(source.data(), source.size(), destination.data(),
                                        destination.size());
}

static void test_reference_manifest() {
    const Bytes source = discovery_response();
    Bytes expected = source;
    // Absolute byte offsets in the existing DHU fixture, including the message
    // ID. This expected result is independent of the production field walker.
    // The two-byte values must stay two bytes, even when their value is zero.
    struct PaddingValue {
        size_t offset;
        Bytes original;
        Bytes cleared;
    };
    const PaddingValue padding[] = {
        {82, {0x90, 0x03}, {0x80, 0x00}},  // margin width: 400
        {85, {0x50}, {0x00}},              // margin height: 80
        {107, {0x28}, {0x00}},             // top inset: 40
        {109, {0x28}, {0x00}},             // bottom inset: 40
        {111, {0xc8, 0x01}, {0x80, 0x00}}, // left inset: 200
        {114, {0xc8, 0x01}, {0x80, 0x00}}  // right inset: 200
    };
    for (const PaddingValue &value : padding) {
        assert(value.original.size() == value.cleared.size());
        for (size_t i = 0; i < value.original.size(); ++i) {
            assert(source[value.offset + i] == value.original[i]);
            expected[value.offset + i] = value.cleared[i];
        }
    }

    Bytes actual;
    assert(rewrite(source, actual));
    assert(actual == expected); // All other channels, lengths and fields stay exact.
    assert(source == discovery_response());

    Bytes repeated;
    assert(rewrite(actual, repeated));
    assert(repeated == actual);

    // A larger output buffer must not have its trailing bytes overwritten.
    Bytes guarded(source.size() + 8, 0xa5);
    assert(
        rewrite_cluster_video_layout(source.data(), source.size(), guarded.data(), guarded.size()));
    assert(Bytes(guarded.begin(), guarded.begin() + source.size()) == expected);
    for (size_t i = source.size(); i < guarded.size(); ++i) {
        assert(guarded[i] == 0xa5);
    }
}

static void test_message_selection() {
    const Bytes source = discovery_response();
    Bytes output;
    Bytes wrong_message = source;
    wrong_message[1] = 5;
    assert(!rewrite(wrong_message, output));
    assert(!rewrite(Bytes(), output));
    assert(!rewrite(Bytes{0}, output));
    assert(!rewrite(Bytes{0, 6}, output));

    output.resize(source.size());
    assert(!rewrite_cluster_video_layout(source.data(), source.size(), output.data(),
                                         source.size() - 1));

    // The reference cluster channel occupies [69, 126); no other channel is 2.
    Bytes missing = source;
    missing.erase(missing.begin() + 69, missing.begin() + 126);
    assert(!rewrite(missing, output));
    Bytes duplicate = source;
    duplicate.insert(duplicate.end(), source.begin() + 69, source.begin() + 126);
    assert(!rewrite(duplicate, output));
}

// Wrap a small test payload in a length-delimited protobuf field.
static Bytes message_field(uint8_t tag, const Bytes &payload) {
    assert(payload.size() < 128);
    Bytes message = {tag, uint8_t(payload.size())};
    message.insert(message.end(), payload.begin(), payload.end());
    return message;
}

static void test_malformed_fields() {
    const Bytes malformed[] = {
        {0x00},                         // invalid field number 0
        {0x80},                         // unterminated tag
        {0x80, 0x80, 0x80, 0x80, 0x10}, // tag exceeds 32 bits
        {0x08, 0x80},                   // unterminated scalar
        {0x08, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
         0},                         // scalar exceeds ten bytes
        {0x12, 0x80},                // unterminated length
        {0x12, 0x03, 0x01, 0x02},    // payload shorter than length
        {0x09, 0, 0, 0, 0, 0, 0, 0}, // truncated fixed64
        {0x0d, 0, 0, 0},             // truncated fixed32
        {0x0b},
        {0x0c},
        {0x0e},
        {0x0f} // unsupported wire types
    };
    for (const Bytes &invalid : malformed) {
        Bytes source = discovery_response();
        source.insert(source.end(), invalid.begin(), invalid.end());
        Bytes output;
        assert(!rewrite(source, output));

        // A bad field deep inside cluster insets must propagate failure through
        // every enclosing message, even though all their byte lengths are valid.
        Bytes extra = message_field(0x0a, invalid); // display insets
        Bytes video = message_field(0x5a, extra);   // additional configuration
        Bytes sink = message_field(0x22, video);    // video configuration
        Bytes channel = {0x08, 0x02};               // cluster channel ID
        const Bytes media_sink = message_field(0x1a, sink);
        channel.insert(channel.end(), media_sink.begin(), media_sink.end());
        const Bytes channels = message_field(0x0a, channel);
        source = {0x00, 0x06};
        source.insert(source.end(), channels.begin(), channels.end());
        assert(!rewrite(source, output));
    }
}

static void test_unknown_fields() {
    // Unknown fields are skipped by wire type; a length-delimited payload is
    // opaque unless it is part of the specific cluster schema path.
    const Bytes unknown = {
        0xf8, 0x01, 0x96, 0x01,                        // field 31: varint
        0xf9, 0x01, 1,    2,    3,    4,   5, 6, 7, 8, // field 31: fixed64
        0xfd, 0x01, 1,    2,    3,    4,               // field 31: fixed32
        0xfa, 0x01, 3,    0xff, 0x00, 0x80             // field 31: opaque bytes
    };
    Bytes source = discovery_response();
    Bytes expected;
    assert(rewrite(source, expected));
    source.insert(source.end(), unknown.begin(), unknown.end());
    expected.insert(expected.end(), unknown.begin(), unknown.end());
    Bytes actual;
    assert(rewrite(source, actual));
    assert(actual == expected);
}

int main() {
    test_reference_manifest();
    test_message_selection();
    test_malformed_fields();
    test_unknown_fields();
    puts("AA_MANIFEST_TEST_OK exact padding rewrite, channel isolation, bounds, malformed fields");
}
