// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#include "../rhmiapp/native_media_hook.hpp"
#include <assert.h>
#include <stdio.h>
using namespace nbt::native_media;
int main() {
    RowShape rows[] = { {1,2,1,1,0}, {2,2,1,1,0} };
    const uint32_t contexts[][2] = {{0xc495,49}, {0xc491,20}};
    for (unsigned c = 0; c != 2; ++c) {
        const uint32_t window = contexts[c][0], mode = contexts[c][1];
        assert(noAudioShape(window,mode,2,rows,kBuild66));
        assert(!noAudioShape(window,mode,1,rows,kBuild66));
        assert(!noAudioShape(window,mode,3,rows,kBuild66));
        assert(!noAudioShape(window,mode,2,0,kBuild66));
        for (unsigned i = 0; i != 2; ++i) {
            RowShape original = rows[i];
            rows[i].id = 9; assert(!noAudioShape(window,mode,2,rows,kBuild66)); rows[i] = original;
            rows[i].type = 3; assert(!noAudioShape(window,mode,2,rows,kBuild66)); rows[i] = original;
            rows[i].format2 = 0; assert(!noAudioShape(window,mode,2,rows,kBuild66)); rows[i] = original;
            rows[i].format1 = 0; assert(!noAudioShape(window,mode,2,rows,kBuild66)); rows[i] = original;
            rows[i].symbol = 1; assert(!noAudioShape(window,mode,2,rows,kBuild66)); rows[i] = original;
        }
    }
    const uint32_t foreignWindows[] = {0,0xc35f,0xc364,0xc43c,0xc43d,0xc48d,49};
    for (unsigned i = 0; i != sizeof(foreignWindows)/sizeof(*foreignWindows); ++i)
        assert(!noAudioShape(foreignWindows[i],20,2,rows,kBuild66));
    const uint32_t foreignModes[] = {0,1,18,19,30,44,49,50};
    for (unsigned i = 0; i != sizeof(foreignModes)/sizeof(*foreignModes); ++i)
        assert(!noAudioShape(0xc491,foreignModes[i],2,rows,kBuild66));
    uint32_t original[20], clone[20] = {};
    for (unsigned i = 0; i != 20; ++i) original[i] = 0x1000 + i;
    original[0] = 0; original[1] = 0x02ec0284; original[18] = 0x0235b634;
    assert(cloneContentVtable(original,0x12345678,clone,kBuild66));
    for (unsigned i = 0; i != 20; ++i)
        assert(clone[i] == (i == 18 ? 0x12345678 : original[i]));
    original[1]++;
    assert(!cloneContentVtable(original,0x12345678,clone,kBuild66));
    original[1]--; original[18]++;
    assert(!cloneContentVtable(original,0x12345678,clone,kBuild66));
    assert(!cloneContentVtable(0,0x12345678,clone,kBuild66));
    assert(!cloneContentVtable(original,0,clone,kBuild66));
    uint32_t info[16], infoClone[16] = {};
    for (unsigned i = 0; i != 16; ++i) info[i] = 0x2000 + i;
    info[0] = 0; info[1] = 0x02ec02b4; info[14] = 0x0235afc0;
    assert(cloneInfoVtable(info,0x87654321,infoClone,kBuild66));
    for (unsigned i = 0; i != 16; ++i)
        assert(infoClone[i] == (i == 14 ? 0x87654321 : info[i]));
    // The receiver and every other native virtual method retain their original slots.
    assert(infoClone[8] == info[8] && infoClone[15] == info[15]);
    info[1]++;
    assert(!cloneInfoVtable(info,0x87654321,infoClone,kBuild66));
    info[1]--; info[14]++;
    assert(!cloneInfoVtable(info,0x87654321,infoClone,kBuild66));
    assert(!cloneInfoVtable(0,0x87654321,infoClone,kBuild66));
    assert(!cloneInfoVtable(info,0,infoClone,kBuild66));
    uint32_t position[16], positionClone[16] = {};
    for (unsigned i = 0; i != 16; ++i) position[i] = 0x3000 + i;
    position[0] = 0; position[1] = 0x02ec0250;
    position[8] = 0x02359b54; position[14] = 0x02359b6c;
    assert(clonePositionVtable(position,0x13572468,positionClone,kBuild66));
    for (unsigned i = 0; i != 16; ++i)
        assert(positionClone[i] == (i == 14 ? 0x13572468 : position[i]));
    assert(positionClone[8] == 0x02359b54); // Public Position Get still rejects via its original receiver.
    position[1]++;
    assert(!clonePositionVtable(position,0x13572468,positionClone,kBuild66));
    position[1]--; position[14]++;
    assert(!clonePositionVtable(position,0x13572468,positionClone,kBuild66));
    assert(!clonePositionVtable(0,0x13572468,positionClone,kBuild66));
    assert(!clonePositionVtable(position,0,positionClone,kBuild66));
    uint16_t subscribers[65], other[65];
    for (unsigned i = 0; i != 65; ++i) subscribers[i] = other[i] = (uint16_t)(0x100 + i);
    assert(sameSubscribers(subscribers,2,other,2));
    assert(sameSubscribers(subscribers,64,other,64));
    assert(!sameSubscribers(subscribers,65,other,65));
    assert(!sameSubscribers(subscribers,0,other,0));
    assert(!sameSubscribers(0,2,other,2));
    assert(!sameSubscribers(subscribers,2,0,2));
    assert(!sameSubscribers(subscribers,2,other,1));
    other[1]++;
    assert(!sameSubscribers(subscribers,2,other,2));
    assert(restoreMask(true,true,true) == 3);
    assert(restoreMask(true,false,true) == 2); // Factory replaced title: restore only our artist.
    assert(restoreMask(true,true,false) == 1);
    assert(restoreMask(true,false,false) == 0);
    assert(restoreMask(false,true,true) == 0); // A different native window cannot receive cached labels.
    puts("native media context/private Content, Info and Position vtables/subscribers/restore: PASS");
}
