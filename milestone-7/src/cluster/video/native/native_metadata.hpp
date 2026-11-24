#pragma once
#include <stdint.h>
#include "cluster/profiles/video.hpp"

namespace kombi {
namespace pip {
namespace screen {

enum {
    FactoryMap = nbt::cluster::VideoProfile::MetadataMap,
    FactoryManeuver = nbt::cluster::VideoProfile::MetadataManeuver,
    FactoryHudMap = nbt::cluster::VideoProfile::MetadataHudMap,
    MetadataBytes = nbt::cluster::VideoProfile::MetadataBytes
};

// Destination must provide MetadataBytes writable bytes. An unknown profile
// returns false before writing anything. The selected hardware profile owns
// the captured metadata bytes and their provenance.
inline bool factory_metadata_bgra(uint8_t* destination, uint16_t picture,
                                  unsigned profile = FactoryMap) {
    return nbt::cluster::VideoProfile::encodeMetadataBgra(destination, picture, profile);
}

} // namespace screen
} // namespace pip
} // namespace kombi
