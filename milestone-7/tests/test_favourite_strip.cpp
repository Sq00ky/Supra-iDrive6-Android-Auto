#include "favourite_strip.hpp"
#include "car/favourite_preview.hpp"
#include <assert.h>
#include <string.h>
#include <stdio.h>
int main() {
    nbt::FavouriteStripRegion r = nbt::favouriteStripRegion(1440, 540, true);
    assert(r.x == 0 && r.y == 64 && r.w == 1440 && r.h == 476);
    assert(r.y + r.h == 540); // only the top strip is uncovered
    // With identical source/destination rectangles, every retained pixel stays at its original coordinate.
    for (int y = r.y; y < 540; ++y) {
        int sourceY = r.y + (y - r.y) * r.h / r.h;
        assert(sourceY == y);
    }
    r = nbt::favouriteStripRegion(1280, 480, true);
    assert(r.y == 57 && r.h == 423);
    r = nbt::favouriteStripRegion(1440, 540, true, 80);
    assert(r.y == 80 && r.h == 460);
    r = nbt::favouriteStripRegion(1440, 540, true, 999);
    assert(r.y == 64 && r.h > 0);
    r = nbt::favouriteStripRegion(1440, 540, false, 80);
    assert(r.y == 0 && r.h == 540); // release/exit returns to the exact full panel, not a scaled image
    char order[80];
    nbt::favouriteRenderOrder(order, sizeof(order), 1282, 0, true);
    assert(!strcmp(order, "0x503,0x502,0x100")); // independent preview above unchanged AA, native source underneath
    nbt::favouriteRenderOrder(order, sizeof(order), 1282, 1281, true);
    assert(!strcmp(order, "0x501,0x503,0x502,0x100")); // volume pill remains above preview and AA
    nbt::favouriteRenderOrder(order, sizeof(order), 1282, 1281, false);
    assert(!strcmp(order, "0x501,0x502"));
    // Touch lifetime still determines only the crop; AA's foreground eligibility stays true throughout.
    nbt::car::FavouritePreview p; uint8_t data[8] = {1};
    p.feed(0x15, 0x0c, 0xc02, 0xc, data, 8, 1000);
    bool foreground = true;
    bool preview = p.step(1100, foreground, foreground, true);
    r = nbt::favouriteStripRegion(1440, 540, preview);
    assert(foreground && preview && r.h > 0);
    memset(data, 0, sizeof(data)); p.feed(0x15, 0x0c, 0xc02, 0xc, data, 8, 3000);
    preview = p.step(3750, foreground, foreground, true);
    r = nbt::favouriteStripRegion(1440, 540, preview);
    assert(foreground && !preview && r.y == 0 && r.h == 540);
    puts("test_favourite_strip OK");
}
