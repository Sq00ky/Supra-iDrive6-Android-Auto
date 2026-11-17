// The native service supplies scaled pixels. Check our buffer request and
// packing contract without pretending to emulate its filter or acceleration.
#include "hardware_peer.hpp"
#include <limits.h>

struct InvalidCapturePeer : HardwarePeer {
    unsigned mode;
    explicit InvalidCapturePeer(unsigned value) : mode(value) {}
    int screen_get_buffer_property_iv(void* buffer, int property, int* value) {
        int result = HardwarePeer::screen_get_buffer_property_iv(buffer, property, value);
        if (property == aa_cluster::hardware::BufferSize) {
            if (mode == 0) value[0] = 800;
            if (mode == 1) value[1] = 480;
        }
        if (property == aa_cluster::hardware::Stride) {
            if (mode == 2) *value = 522 * 4 - 1;
            if (mode == 3) *value = 65537;
        }
        if (mode == 4 && property == aa_cluster::hardware::Format) *value = 12;
        return result;
    }
};

int main() {
    typedef HardwarePeer::P P;
    assert(P::FittedWidth == 522 && P::ContentHeight == 313 && P::PaddingX == 49);
    const unsigned guard = 16, stride = 522 * 4 + 13, bytes = 620 * 313 * 4;
    std::vector<uint8_t> source(guard + stride * 313 + guard, 0x9b);
    std::vector<uint8_t> output(guard + bytes + guard, 0xd6);
    for (unsigned y = 0; y < 313; ++y) for (unsigned x = 0; x < 522; ++x) {
        uint8_t* pixel = &source[guard + y * stride + x * 4];
        pixel[0] = x % 251; pixel[1] = y % 239; pixel[2] = (x + y) % 233;
        pixel[3] = (x ^ y) % 256; // Output must retain the old opaque-alpha contract.
    }
    const std::vector<uint8_t> unchanged_source = source;
    assert(aa_cluster::pack_scaled_hardware_picture(&source[guard], 522, 313, stride, &output[guard]));
    for (unsigned y = 0; y < 313; ++y) for (unsigned x = 0; x < 620; ++x) {
        const uint8_t* pixel = &output[guard + (y * 620 + x) * 4];
        assert(pixel[3] == 255);
        if (x < 49 || x >= 571) assert(pixel[0] == 0 && pixel[1] == 0 && pixel[2] == 0);
        else assert(!memcmp(pixel, &source[guard + y * stride + (x - 49) * 4], 3));
    }
    for (unsigned i = 0; i < guard; ++i) {
        assert(output[i] == 0xd6 && output[guard + bytes + i] == 0xd6);
    }
    assert(source == unchanged_source); // No writes to source pixels or stride padding.
    const std::vector<uint8_t> unchanged_output = output;
    for (unsigned mode = 0; mode < 8; ++mode) {
        unsigned width = 522, height = 313, row = stride;
        const uint8_t* input = &source[guard]; uint8_t* destination = &output[guard];
        switch (mode) {
            case 0: width = 800; break;
            case 1: height = 480; break;
            case 2: row = 522 * 4 - 1; break;
            case 3: row = UINT_MAX; break;
            case 4: width = UINT_MAX; break;
            case 5: height = 0; break;
            case 6: input = 0; break;
            case 7: destination = 0; break;
        }
        assert(!aa_cluster::pack_scaled_hardware_picture(input, width, height, row, destination));
        assert(output == unchanged_output);
    }
    for (unsigned mode = 0; mode < 5; ++mode) {
        InvalidCapturePeer peer(mode);
        aa_cluster::hardware::Session<InvalidCapturePeer> session(peer);
        assert(!session.open());
        session.close(); assert(peer.objects.empty() && peer.reads == 0);
    }
    puts("PASS 522x313 capture request, full pixel/alpha/49px-bar packing, odd stride, guards and invalid-buffer cleanup");
}
