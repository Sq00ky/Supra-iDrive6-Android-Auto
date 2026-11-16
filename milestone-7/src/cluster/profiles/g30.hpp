#pragma once

#include <stdint.h>

namespace nbt {
namespace cluster {

// Physical G30 output contract verified on NBT EVO 007.039.008. Geometry,
// endpoints and metadata belong here; the video transport and Screen producer
// consume this profile without selecting hardware by name.
struct G30Video {
    enum {
        SourceWidth = 800,
        SourceHeight = 480,
        BackingWidth = 800,
        BackingHeight = 480,
        ContentWidth = 620,
        ContentHeight = 313,
        ContentY = 1,
        FittedWidth = 522,
        PaddingX = 49,
        CompactWidth = 310,
        CompactHeight = 157,
        SinkSelection = 1,
        // Valid alternate sink captured from the factory HUD. We pause on it;
        // this profile still produces only cluster video.
        HudWidth = 277,
        HudHeight = 160,
        HudSelection = 0,
        // 93 identical factory method-3 writes in 20261114T143330Z-4cdd29
        // configured this backing with color-depth 0 and packaging 1.
        ColorDepth = 0,
        Packaging = 1,

        DisplayCount = 2,
        DisplayIndex = 1,
        ScreenDisplayId = 2,
        WindowIdLength = 9,
        ScreenFormat = 8,
        ScreenUsage = 6,
        ScreenTransparency = 0,
        ScreenAlpha = 255,
        ScreenZOrder = 1000,
        ScreenContextFlags = 1,

        // IPv4 bytes in the little-endian QNX ARM sockaddr representation.
        ProducerIP = 0x78c730a0,
        ClusterIP = 0x60c730a0,
        GuidancePort = 30502,
        DisplaySinkPort = 30503,
        DiscoveryPort = 30490,

        MetadataMap = 0,
        MetadataManeuver = 1,
        MetadataBytes = 84
    };

    static const char* producerAddress() {
        return "160.48.199.120";
    }
    static const char* clusterAddress() {
        return "160.48.199.96";
    }
    static const char* headUnitAddress() {
        return "160.48.199.119";
    }
    static const char* networkInterface() {
        return "vlan73";
    }
    // Scene display index 1 in the high nibble, private surface 9000.
    static const char* windowId() {
        return "268444456";
    }
    static const char* factoryWindowId() {
        return "1056";
    }
    static const char* nodeName() {
        return "hu-omap";
    }

    // Exact first-84-byte templates from paired factory window-1056 BGRA
    // BMPs in 20261114T143330Z-4cdd29; see test_factory_capture.py. Only the
    // BE16 picture counter varies within each profile. BMP export is evidence
    // for this output contract, not a physical wire-byte capture.
    static bool encodeMetadataBgra(uint8_t* destination, uint16_t picture,
                                   unsigned profile = MetadataMap) {
        if (profile != MetadataMap && profile != MetadataManeuver) {
            return false;
        }
        for (unsigned i = 0; i < MetadataBytes; ++i) {
            destination[i] = 0;
        }
        destination[0] = uint8_t(picture >> 8);
        destination[1] = uint8_t(picture);
        destination[4] = 1;
        destination[7] = 1;
        if (profile == MetadataMap) {
            destination[18] = 0x0f;
            destination[19] = 0xa2;
        } else {
            destination[16] = 0x0f;
        }
        return true;
    }
};

} // namespace cluster
} // namespace nbt
