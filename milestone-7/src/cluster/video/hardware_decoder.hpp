#pragma once
#include <stdint.h>

namespace aa_cluster {

// Owns one native mm-renderer context. Output arrives asynchronously, including
// after the phone stops sending an unchanged map. A worker reads scaled pictures;
// poll only takes the newest completed picture and never waits for readback.
class HardwareDecoder {
    struct Implementation;
    Implementation* implementation_;
    HardwareDecoder(const HardwareDecoder&);
    HardwareDecoder& operator=(const HardwareDecoder&);
public:
    HardwareDecoder();
    ~HardwareDecoder();
    bool open();
    void close();
    int decode(const uint8_t*, unsigned, uint64_t, uint8_t*, bool = true);
    int poll_picture(uint8_t*, bool = true);
    unsigned unchanged() const;
    unsigned coalesced() const;
    // Supervisor-only recovery of a reaped child's exact renderer context.
    static int cleanup_owner(const char* process_id);
};
} // namespace aa_cluster
