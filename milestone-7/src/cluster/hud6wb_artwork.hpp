// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef CLUSTER_HUD6WB_ARTWORK_HPP
#define CLUSTER_HUD6WB_ARTWORK_HPP
#include "cluster/hudterm_shapes.hpp"
namespace nbt { namespace hudterm {
// Outlined arrows adapted from JayJay3071's offset-path renderer; coordinates
// fitted to an explicit profile rectangle. No OEM assets or binary dependencies.
Shape bmwManeuver(const ShapeSpec& spec, const Rect& rect, bool knownExit = true);
Shape bmwExitNumber(unsigned number, const Rect& rect);
Shape bmwLane(int shape, bool highlighted, const Rect& rect);
} }
#endif
