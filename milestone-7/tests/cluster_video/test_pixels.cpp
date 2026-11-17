#include "cluster/video/hardware_decoder.hpp"
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

static unsigned test_decoded_pixels_and_metadata(HardwareDecoder &decoder) {
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
        expect(decoder.decode(encoded_payload, payload_size, be64(wire_header + 16), decoded_pixels) == 0,
               6, "renderer accepts moving-video access unit");
        expect(decoder.poll_picture(decoded_pixels) == 1, 6, "retrieve completed renderer picture");
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

int main() {
    HardwareDecoder decoder;
    unsigned presented_frames = test_decoded_pixels_and_metadata(decoder);
    printf("AA_PIXEL_CONTRACT_OK frames=%u factory_metadata row0 BGRA content_origin=0,1 "
           "unused_backing_zero\n",
           presented_frames);
}
