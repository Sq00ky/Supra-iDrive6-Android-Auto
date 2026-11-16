#pragma once
#include <stdint.h>
#include <string.h>

namespace nbt {
namespace cluster {

// The shared Guidance driver needs an endpoint and the existing controller
// handle. Video creates its own controller and has a separate output profile.
struct GuidanceProfile {
    const char* name;
    const char* host;
    uint16_t port;
    uint32_t handle;
};

// Both vehicles use BMW.IC.Guidance. The graph originated in a J29 capture;
// the G30 endpoint and interoperability were subsequently checked on the car.
static const GuidanceProfile J29Guidance = {"j29", "160.48.199.96", 30502, 0xC8};
static const GuidanceProfile G30Guidance = {"g30", "160.48.199.96", 30502, 0xC8};

inline const GuidanceProfile& buildGuidanceProfile() {
#ifdef NBT_CLUSTER_PROFILE_G30
    return G30Guidance;
#else
    return J29Guidance;
#endif
}

inline const GuidanceProfile* guidanceProfile(const char* name) {
    if (!strcmp(name, J29Guidance.name))
        return &J29Guidance;
    if (!strcmp(name, G30Guidance.name))
        return &G30Guidance;
    return 0; // The MOST/6WA backend is selected separately.
}

} // namespace cluster
} // namespace nbt
