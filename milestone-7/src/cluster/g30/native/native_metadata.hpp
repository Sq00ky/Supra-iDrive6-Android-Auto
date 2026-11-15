#pragma once
#include <stdint.h>

namespace kombi { namespace pip { namespace screen {
enum { FactoryMap=0, FactoryManeuver=1, MetadataBytes=84 };
// Exact first-84-byte templates from the two pairs of factory window-1056
// BGRA BMPs in 20261114T143330Z-4cdd29; see test_factory_capture.py.
// Only the BE16 picture counter changes. In particular byte5 stays zero and
// bytes16..19 stay as captured, without guessing their varying semantics.
// BMP export is evidence for this experiment, not a physical wire-byte capture.
inline bool factory_metadata_bgra(uint8_t* out, uint16_t picture,
                                  unsigned profile=FactoryMap) {
    if(profile!=FactoryMap && profile!=FactoryManeuver) return false;
    for(unsigned i=0;i<MetadataBytes;++i) out[i]=0;
    out[0]=uint8_t(picture>>8); out[1]=uint8_t(picture);
    out[4]=1; out[7]=1;
    if(profile==FactoryMap) { out[18]=0x0f; out[19]=0xa2; }
    else out[16]=0x0f;
    return true;
}
}}}
