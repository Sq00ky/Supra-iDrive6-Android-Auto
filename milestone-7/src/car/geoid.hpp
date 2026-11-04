// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef CAR_GEOID_HPP
#define CAR_GEOID_HPP

#include <stdint.h>

namespace nbt {
namespace car {

// Height of sea level (the EGM2008 geoid) above the WGS84 ellipsoid at a position, in cm, interpolated from a 5 degree grid.
// Against EGM2008's 5 minute grid the error is 0.5 m at the median in the UK (at most 2.6 m) and 0.7 m worldwide, and reaches
// about 22 m where the geoid changes fast, such as around Japan and Indonesia. Out-of-range input is held at the edge.
int32_t geoidSeparationCm(int32_t latE7, int32_t lonE7);

} // namespace car
} // namespace nbt

#endif
