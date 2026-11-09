// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Host test for src/car_state.hpp: the file the iDrive app writes and the daemon reads at start.
#include "car_state.hpp"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

using namespace nbt::carstate;

static void testFormat() {
    const Item items[] = { { "gear", 2 }, { "pdc", 0 }, { "mode1", -1 } };
    char text[kMaxBytes];
    int n = format(text, sizeof(text), items, 3);
    assert(n == 3 * 17);
    assert(strcmp(text, "gear           2\npdc            0\nmode1         -1\n") == 0);

    // every line stays in place when a value changes, so a rewrite can't shift the next one
    const Item changed[] = { { "gear", 12345 }, { "pdc", -7 }, { "mode1", 63 } };
    char other[kMaxBytes];
    assert(format(other, sizeof(other), changed, 3) == n);
    assert(other[17] == 'p' && other[34] == 'm');

    const Item longest[] = { { "123456789", 1 } };
    assert(format(text, sizeof(text), longest, 1) == 17);
    const Item tooLong[] = { { "1234567890", 1 } };
    assert(format(text, sizeof(text), tooLong, 1) == -1);
    const Item empty[] = { { "", 1 } };
    assert(format(text, sizeof(text), empty, 1) == -1);
    const Item none[] = { { NULL, 1 } };
    assert(format(text, sizeof(text), none, 1) == -1);

    volatile size_t cap = 17;                    // volatile: truncating on purpose, GCC warns about a constant size
    assert(format(text, cap, items, 1) == -1);   // no room for the terminator
    cap = 18;
    assert(format(text, cap, items, 1) == 17);
    cap = 20;
    assert(format(text, cap, items, 3) == -1);
    assert(format(text, sizeof(text), items, 0) == 0);
    assert(text[0] == 0);
}

// The limit the iDrive app's table is checked against at compile time has to be the real one.
static void testMaxItems() {
    assert(kLineBytes == 17);
    Item items[kMaxItems + 1];
    for (int i = 0; i <= kMaxItems; ++i) {
        items[i].key = "123456789";
        items[i].value = -99999;    // the widest value that keeps a line fixed
    }
    char text[kMaxBytes];
    assert(format(text, sizeof(text), items, kMaxItems) == kMaxItems * kLineBytes);
    assert(format(text, sizeof(text), items, kMaxItems + 1) == -1);
}

static void testParse() {
    const char* text = "gear           2\npdc            0\nmode0          4\nmode1         -1\n";
    int v = 99;
    assert(parse(text, "gear", &v) && v == 2);
    assert(parse(text, "pdc", &v) && v == 0);
    assert(parse(text, "mode0", &v) && v == 4);
    assert(parse(text, "mode1", &v) && v == -1);

    v = 99;
    assert(!parse(text, "daynight", &v) && v == 99);    // an older app doesn't have every key
    assert(!parse(text, "mode", &v) && v == 99);        // a prefix of a key is not the key
    assert(!parse(text, "gea", &v) && v == 99);
    assert(!parse(text, "ear", &v) && v == 99);         // nor is the tail of one

    // a newer app's extra keys and a line without a newline at the end
    assert(parse("future  123\ngear 5", "gear", &v) && v == 5);
    assert(parse("gear 5\nfuture  123\n", "future", &v) && v == 123);

    // text that isn't a number never becomes a value, such as a rewrite caught half way
    v = 99;
    assert(!parse("gear      \n", "gear", &v) && v == 99);
    assert(!parse("gear       x\n", "gear", &v) && v == 99);
    assert(!parse("gear       2x\n", "gear", &v) && v == 99);
    assert(!parse("", "gear", &v) && v == 99);
    assert(!parse(NULL, "gear", &v));
    assert(!parse(text, NULL, &v));
    assert(!parse(text, "", &v));
    assert(!parse(text, "gear", NULL));
}

static void testValueAfterKey() {
    assert(strcmp(valueAfterKey("gear 2", NBT_CAR_KEY_GEAR), "2") == 0);
    assert(strcmp(valueAfterKey("pdc -1", NBT_CAR_KEY_PDC), "-1") == 0);
    assert(strcmp(valueAfterKey("daynight 3", NBT_CAR_KEY_DAYNIGHT), "3") == 0);
    assert(strcmp(valueAfterKey("dsc 2 1", NBT_CAR_KEY_DSC), "2 1") == 0);
    assert(strcmp(valueAfterKey("gear ", NBT_CAR_KEY_GEAR), "") == 0);

    assert(!valueAfterKey("gear", NBT_CAR_KEY_GEAR));        // no separator, no value
    assert(!valueAfterKey("gear2 1", NBT_CAR_KEY_GEAR));     // a longer key isn't this key
    assert(!valueAfterKey("gea 1", NBT_CAR_KEY_GEAR));
    assert(!valueAfterKey("pdc 1", NBT_CAR_KEY_GEAR));
    assert(!valueAfterKey("", NBT_CAR_KEY_GEAR));
    assert(!valueAfterKey(NULL, NBT_CAR_KEY_GEAR));
    assert(!valueAfterKey("gear 2", NULL));
    assert(!valueAfterKey(" 2", ""));
}

static void testFile() {
    char path[64];
    snprintf(path, sizeof(path), "tests/build/test_car_state.%d.tmp", (int)getpid());
    unlink(path);
    char text[kMaxBytes];
    assert(!readFile(path, text, sizeof(text)));
    assert(text[0] == 0);

    const Item three[] = { { "gear", 2 }, { "pdc", 0 }, { "daynight", 1 } };
    assert(writeFile(path, three, 3));
    assert(readFile(path, text, sizeof(text)));
    int v = 99;
    assert(parse(text, "gear", &v) && v == 2);
    assert(parse(text, "daynight", &v) && v == 1);

    struct stat before, after;
    assert(stat(path, &before) == 0);
    assert(before.st_size == 3 * 17);
    assert((before.st_mode & 0777) == 0666);

    // A reader already holding the file open sees the new text with no reopening: the file is rewritten, not replaced.
    FILE* held = fopen(path, "r");
    assert(held);

    // a shorter text leaves nothing of the old one behind
    const Item one[] = { { "gear", 3 } };
    assert(writeFile(path, one, 1));
    assert(stat(path, &after) == 0);
    assert(after.st_ino == before.st_ino);
    assert(after.st_size == 17);
    assert(readFile(path, text, sizeof(text)));
    assert(parse(text, "gear", &v) && v == 3);
    assert(!parse(text, "daynight", &v));

    char seen[kMaxBytes];
    size_t got = fread(seen, 1, sizeof(seen) - 1, held);
    seen[got] = 0;
    assert(got == 17 && parse(seen, "gear", &v) && v == 3);
    fclose(held);

    // a text that doesn't fit leaves the file as it was
    const Item bad[] = { { "much-too-long-a-key", 1 } };
    assert(!writeFile(path, bad, 1));
    assert(readFile(path, text, sizeof(text)));
    assert(parse(text, "gear", &v) && v == 3);

    // a short buffer still ends in a terminator
    char small[6];
    assert(readFile(path, small, sizeof(small)));
    assert(strlen(small) == 5);
    assert(!readFile(path, small, 0));

    // an empty file counts as no file
    FILE* f = fopen(path, "w");
    assert(f);
    fclose(f);
    assert(!readFile(path, text, sizeof(text)));

    unlink(path);
    assert(!writeFile("tests/build/no-such-dir/file", one, 1));
}

int main() {
    testFormat();
    testMaxItems();
    testParse();
    testValueAfterKey();
    testFile();
    printf("test_car_state OK\n");
    return 0;
}
