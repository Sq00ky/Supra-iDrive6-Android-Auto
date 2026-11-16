#include "cluster/profiles/guidance.hpp"
#include "cluster/guidance/graph.hpp"
#ifdef NBT_CLUSTER_PROFILE_G30
#include "cluster/profiles/video.hpp"
#include "cluster/video/native/presentation.hpp"
#include "cluster/video/native/factory_profile.hpp"
#endif
#include <assert.h>
#include <string.h>

int main() {
    using namespace nbt::cluster;
    assert(guidanceProfile("j29") == &J29Guidance);
    assert(guidanceProfile("g30") == &G30Guidance);
    assert(!guidanceProfile("unsupported"));
    assert(!guidanceProfile("6wa")); // Uses a different transport.
#ifdef NBT_CLUSTER_PROFILE_G30
    assert(&buildGuidanceProfile() == &G30Guidance);
    assert(VideoProfile::BackingWidth == 800 && VideoProfile::BackingHeight == 480);
    assert(VideoProfile::ContentWidth == 620 && VideoProfile::ContentHeight == 313);
    assert(VideoProfile::FittedWidth + 2 * VideoProfile::PaddingX == VideoProfile::ContentWidth);
    const kombi::pip::detail::NativePipResource resource = native_pip::resource();
    assert(resource.width == 800 && resource.height == 480);
    assert(resource.color_depth == 0 && resource.packaging == 1);
#else
    assert(&buildGuidanceProfile() == &J29Guidance);
#endif
    // Both turn instructions and video consume this one captured graph.
    assert(nbt::kombinav::tmpl::phase_b_count == 97);
    assert(nbt::kombinav::tmpl::cancel_count == 6);
    return 0;
}
