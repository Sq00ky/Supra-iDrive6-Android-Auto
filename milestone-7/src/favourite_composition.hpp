// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef NBT_FAVOURITE_COMPOSITION_HPP
#define NBT_FAVOURITE_COMPOSITION_HPP
#include <dlfcn.h>
#include <stdio.h>
namespace nbt {
// Keep the native capture source behind AA in the same committed composition.
// No source/destination rectangle, viewport or AA visibility changes here.
class FavouriteComposition {
    typedef int (*NoArgs)();
    typedef int (*Visibility)(unsigned, unsigned);
    typedef int (*RenderOrder)(unsigned, const unsigned*, unsigned);
    void* library_;
    NoArgs destroy_, commit_;
    Visibility visibility_;
    RenderOrder order_;
    bool ready_;
public:
    FavouriteComposition() : library_(NULL), destroy_(NULL), commit_(NULL),
        visibility_(NULL), order_(NULL), ready_(false) {}
    bool initialize() {
        if (ready_) return true;
        library_ = dlopen("/opt/sys/lib/libilmClient.gpp.so.1", RTLD_NOW | RTLD_LOCAL);
        if (!library_) return false;
        NoArgs init = (NoArgs)dlsym(library_, "ilm_init");
        destroy_ = (NoArgs)dlsym(library_, "ilm_destroy");
        commit_ = (NoArgs)dlsym(library_, "ilm_commitChanges");
        visibility_ = (Visibility)dlsym(library_, "ilm_surfaceSetVisibility");
        order_ = (RenderOrder)dlsym(library_, "ilm_layerSetRenderOrder");
        if (!init || !destroy_ || !commit_ || !visibility_ || !order_ || init()) {
            dlclose(library_); library_ = NULL; return false;
        }
        ready_ = true;
        return true;
    }
    ~FavouriteComposition() {
        if (ready_) destroy_();
        if (library_) dlclose(library_);
    }
    bool apply(unsigned aa, int pill, bool preview) {
        if (!initialize()) return false;
        unsigned surfaces[4]; unsigned n = 0;
        if (pill > 0) surfaces[n++] = (unsigned)pill;
        if (preview) surfaces[n++] = 1283;
        surfaces[n++] = aa;
        if (preview) surfaces[n++] = 256;
        int v = visibility_(256, preview ? 1 : 0);
        int o = order_(0x80000000u, surfaces, n);
        int c = (v == 0 && o == 0) ? commit_() : -1;
        if (v || o || c) {
            printf("[Favourites] atomic composition failed visibility=%d order=%d commit=%d\n", v, o, c);
            // Discard this client's pending work before using the established CLI path.
            destroy_(); ready_ = false;
            dlclose(library_); library_ = NULL;
            return false;
        }
        return true;
    }
private:
    FavouriteComposition(const FavouriteComposition&);
    FavouriteComposition& operator=(const FavouriteComposition&);
};
}
#endif
