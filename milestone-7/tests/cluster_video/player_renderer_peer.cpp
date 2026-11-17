// Link the player to the production renderer session with deterministic Screen
// pictures. Advance capture once per submitted frame, on a later player poll.
#include "hardware_peer.hpp"
#include "cluster/video/hardware_decoder.hpp"

namespace aa_cluster {
struct HardwareDecoder::Implementation {
    HardwarePeer api;
    hardware::Session<HardwarePeer> session;
    bool pending;

    Implementation() : session(api), pending(false) {}
};

HardwareDecoder::HardwareDecoder() : implementation_(new Implementation) {}
HardwareDecoder::~HardwareDecoder() { delete implementation_; }
bool HardwareDecoder::open() {
    implementation_->pending = false;
    return implementation_->session.open();
}
void HardwareDecoder::close() {
    implementation_->pending = false;
    implementation_->session.close();
}
int HardwareDecoder::decode(const uint8_t* data, unsigned size, uint64_t pts,
                            uint8_t* pixels, bool present) {
    int result = implementation_->session.decode(data, size, pts, pixels, present);
    if (result == 0) implementation_->pending = true;
    return result;
}
int HardwareDecoder::poll_picture(uint8_t* pixels, bool present) {
    if (!implementation_->pending || !present) return 0;
    implementation_->api.clock += 100000;
    implementation_->pending = false;
    return implementation_->session.poll(pixels);
}
unsigned HardwareDecoder::unchanged() const { return implementation_->session.unchanged(); }
unsigned HardwareDecoder::coalesced() const { return implementation_->session.coalesced(); }
} // namespace aa_cluster
