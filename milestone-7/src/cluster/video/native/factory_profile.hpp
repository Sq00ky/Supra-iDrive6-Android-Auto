#pragma once
#include "presentation.hpp"
#include "cluster/profiles/video.hpp"

namespace nbt {
namespace cluster {
namespace native_pip {

// Adapt the selected hardware profile to the native Guidance resource body.
// Its backing geometry is independent of the negotiated visible sink area.
inline kombi::pip::detail::NativePipResource resource() {
    kombi::pip::detail::NativePipResource value = {
        VideoProfile::BackingWidth, VideoProfile::BackingHeight, VideoProfile::ColorDepth,
        VideoProfile::Packaging};
    return value;
}

} // namespace native_pip
} // namespace cluster
} // namespace nbt
