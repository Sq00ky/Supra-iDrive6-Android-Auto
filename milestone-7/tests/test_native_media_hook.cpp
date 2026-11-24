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
        assert(noAudioShape(window,mode,2,rows));
        assert(!noAudioShape(window,mode,1,rows));
        assert(!noAudioShape(window,mode,3,rows));
        assert(!noAudioShape(window,mode,2,0));
        for (unsigned i = 0; i != 2; ++i) {
            RowShape original = rows[i];
            rows[i].id = 9; assert(!noAudioShape(window,mode,2,rows)); rows[i] = original;
            rows[i].type = 3; assert(!noAudioShape(window,mode,2,rows)); rows[i] = original;
            rows[i].format2 = 0; assert(!noAudioShape(window,mode,2,rows)); rows[i] = original;
            rows[i].format1 = 0; assert(!noAudioShape(window,mode,2,rows)); rows[i] = original;
            rows[i].symbol = 1; assert(!noAudioShape(window,mode,2,rows)); rows[i] = original;
        }
    }
    const uint32_t foreignWindows[] = {0,0xc35f,0xc364,0xc43c,0xc43d,0xc48d,49};
    for (unsigned i = 0; i != sizeof(foreignWindows)/sizeof(*foreignWindows); ++i)
        assert(!noAudioShape(foreignWindows[i],20,2,rows));
    const uint32_t foreignModes[] = {0,1,18,19,30,44,49,50};
    for (unsigned i = 0; i != sizeof(foreignModes)/sizeof(*foreignModes); ++i)
        assert(!noAudioShape(0xc491,foreignModes[i],2,rows));
    uint32_t original[20], clone[20] = {};
    for (unsigned i = 0; i != 20; ++i) original[i] = 0x1000 + i;
    original[0] = 0; original[1] = 0x02ec0284; original[18] = 0x0235b634;
    assert(cloneContentVtable(original,0x12345678,clone));
    for (unsigned i = 0; i != 20; ++i)
        assert(clone[i] == (i == 18 ? 0x12345678 : original[i]));
    original[1]++;
    assert(!cloneContentVtable(original,0x12345678,clone));
    original[1]--; original[18]++;
    assert(!cloneContentVtable(original,0x12345678,clone));
    assert(restoreMask(true,true,true) == 3);
    assert(restoreMask(true,false,true) == 2); // Factory replaced title: restore only our artist.
    assert(restoreMask(true,true,false) == 1);
    assert(restoreMask(true,false,false) == 0);
    assert(restoreMask(false,true,true) == 0); // A different native window cannot receive cached labels.
    puts("native media No Audio guard/private vtable/restore: PASS");
}
