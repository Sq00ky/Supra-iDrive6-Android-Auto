#include "cluster/video/decoder.hpp"
#include "cluster/video/protocol.hpp"
#include "cluster/video/native/native_screen.hpp"
#include "fake_screen.hpp"

extern "C" int usleep(unsigned) {
    return 0;
}

using namespace aa_cluster;
static uint8_t encoded_payload[MaxPayload];
static uint8_t decoded_pixels[620 * 313 * 4];

static void expect(bool passed, int failure_code, const char *detail) {
    if (!passed) {
        fprintf(stderr, "FAIL pixel contract (%d): %s\n", failure_code, detail);
        exit(failure_code);
    }
}

static unsigned test_decoded_pixels_and_metadata(Decoder &decoder) {
    FILE *fixture = fopen("fixture.acp", "rb");
    expect(fixture != 0, 1, "open moving-video fixture");
    screen_peer::reset();
    kombi::pip::screen::ScreenOutput screen = {};
    expect(screen.inspect(1) && screen.create(), 2, "create secondary Screen output");

    uint8_t wire_header[HeaderSize];
    unsigned presented_frames = 0;
    while (fread(wire_header, 1, sizeof(wire_header), fixture) == sizeof(wire_header)) {
        expect(valid_header(wire_header), 3, "moving-video packet header");
        unsigned payload_size = be32(wire_header + 12);
        unsigned kind = be32(wire_header + 4);
        if (payload_size) {
            expect(fread(encoded_payload, 1, payload_size, fixture) == payload_size, 4,
                   "read complete moving-video access unit");
        }
        if (kind == Reset) {
            expect(decoder.open(), 5, "open moving-video decoder");
            continue;
        }
        if (kind == Stop) {
            decoder.close();
            continue;
        }
        int result =
            decoder.decode(encoded_payload, payload_size, be64(wire_header + 16), decoded_pixels);
        expect(result >= 0, 6, "decode moving-video access unit");
        if (!result) {
            continue;
        }
        expect(screen.present(decoded_pixels, 0x5678 + presented_frames, false), 7,
               "present decoded frame");
        ++presented_frames;

        // Content starts one row below the captured metadata. All unused
        // backing bytes must remain transparent, including the rest of row 0.
        for (unsigned y = 0; y < 480; ++y) {
            for (unsigned x = 0; x < 3200; ++x) {
                uint8_t actual = screen_peer::pixels[y * 3200 + x];
                if (y >= 1 && y <= 313 && x < 620 * 4) {
                    expect(actual == decoded_pixels[(y - 1) * 620 * 4 + x], 8,
                           "decoded content survives metadata-row offset");
                } else if (y) {
                    expect(actual == 0, 9, "unused content backing is zero");
                } else if (x >= 84) {
                    expect(actual == 0, 10, "reserved metadata row is zero");
                }
            }
        }
        const uint8_t *metadata = screen_peer::pixels;
        expect(metadata[0] == ((0x5677 + presented_frames) >> 8) &&
                   metadata[1] == uint8_t(0x5677 + presented_frames) && metadata[4] == 1 &&
                   metadata[7] == 1 && metadata[18] == 15 && metadata[19] == 162,
               11, "captured factory metadata bytes and frame sequence");
    }
    fclose(fixture);
    expect(screen.close() && presented_frames == 30, 12,
           "release Screen after presenting all thirty frames");
    return presented_frames;
}

static void test_unchanged_frames_after_restart(Decoder &decoder) {
    // Identical images still advance decoder references. Each fresh session
    // must present its first frame even when the previous image was identical.
    for (unsigned pass = 0; pass < 2; ++pass) {
        FILE *fixture = fopen("static.acp", "rb");
        expect(fixture != 0, 13, "open static-video fixture");
        uint8_t wire_header[HeaderSize];
        unsigned input_frames = 0;
        unsigned changed_frames = 0;
        while (fread(wire_header, 1, sizeof(wire_header), fixture) == sizeof(wire_header)) {
            expect(valid_header(wire_header), 14, "static-video packet header");
            unsigned payload_size = be32(wire_header + 12);
            unsigned kind = be32(wire_header + 4);
            if (payload_size) {
                expect(fread(encoded_payload, 1, payload_size, fixture) == payload_size, 15,
                       "read complete static-video access unit");
            }
            if (kind == Reset) {
                expect(decoder.open(), 16, "open static-video decoder after restart");
                continue;
            }
            if (kind == Stop) {
                decoder.close();
                continue;
            }
            ++input_frames;
            int result = decoder.decode(encoded_payload, payload_size, be64(wire_header + 16),
                                        decoded_pixels);
            expect(result >= 0, 17, "decode static-video access unit");
            if (result) {
                ++changed_frames;
            }
        }
        fclose(fixture);
        expect(input_frames == 30 && changed_frames == 1, 18,
               "only the first static frame is presented in each session");
    }
    expect(decoder.unchanged() == 58, 19, "unchanged counter spans both static sessions");
}

static void test_coalescing_preserves_predictive_chain(Decoder &decoder) {
    FILE *fixture = fopen("fixture.acp", "rb");
    expect(fixture != 0, 20, "open coalescing fixture");
    uint8_t wire_header[HeaderSize];
    unsigned input_frames = 0;
    unsigned presented_frames = 0;
    while (fread(wire_header, 1, sizeof(wire_header), fixture) == sizeof(wire_header)) {
        unsigned payload_size = be32(wire_header + 12);
        unsigned kind = be32(wire_header + 4);
        if (payload_size) {
            expect(fread(encoded_payload, 1, payload_size, fixture) == payload_size, 21,
                   "read complete coalesced access unit");
        }
        if (kind == Reset) {
            expect(decoder.open(), 22, "open coalescing decoder");
            continue;
        }
        if (kind == Stop) {
            decoder.close();
            continue;
        }
        // Decode every P-frame, but only materialize the first and final image.
        bool should_present = input_frames == 0 || input_frames == 29;
        ++input_frames;
        int result = decoder.decode(encoded_payload, payload_size, be64(wire_header + 16),
                                    decoded_pixels, should_present);
        expect(result >= 0 && (should_present || !result), 23,
               "skipped presentations still decode successfully");
        if (result) {
            ++presented_frames;
        }
    }
    fclose(fixture);
    expect(presented_frames == 2 && decoder.coalesced() == 28, 24,
           "thirty frames decoded with only two presentations");
}

int main() {
    Decoder decoder;
    unsigned presented_frames = test_decoded_pixels_and_metadata(decoder);
    test_unchanged_frames_after_restart(decoder);
    test_coalescing_preserves_predictive_chain(decoder);
    printf("AA_PIXEL_CONTRACT_OK frames=%u factory_metadata row0 BGRA content_origin=0,1 "
           "unused_backing_zero\n",
           presented_frames);
    printf("AA_STATIC_VIDEO_OK unchanged=58 first_frame_after_restart_presented=1\n");
    printf("AA_COALESCED_VIDEO_OK decoded=30 presented=2 predictive_chain_preserved=1\n");
}
