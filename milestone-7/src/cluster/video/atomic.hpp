#pragma once

#include "native/atomic.hpp"

namespace aa_cluster {

// Use the native port's ordering for the two SPSC rings and worker state.
using kombi::pip::detail::atomic_exchange;
using kombi::pip::detail::atomic_load;
using kombi::pip::detail::atomic_store;

} // namespace aa_cluster
