#pragma once

#if defined(NBT_CLUSTER_PROFILE_G30)
#include "g30.hpp"

namespace nbt {
namespace cluster {
typedef G30Video VideoProfile;
} // namespace cluster
} // namespace nbt

#else
#error Cluster video needs a supported profile; define NBT_CLUSTER_PROFILE_G30 for G30
#endif
