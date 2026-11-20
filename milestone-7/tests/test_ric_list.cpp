// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Host test: KOMBI list-service frames (src/cluster/ric_list.hpp), byte for byte against what iDrive sent in the car
// capture of 2026-11-10 (GR Supra, Bluetooth audio).
#include "cluster/ric_list.hpp"
#include <assert.h>
#include <stdio.h>
#include <string.h>
using namespace nbt;
using namespace nbt::riclist;

static Bytes hex(const char* s) {
    Bytes b;
    for (; s[0] && s[1]; s += 2) {
        unsigned v = 0;
        sscanf(s, "%2x", &v);
        b.push_back((uint8_t)v);
    }
    return b;
}

int main() {
    Bytes f;
    // captured: session 0x000c, model 0x03000002
    assert(textFrame(0x64, 2, "Previous title", f));
    assert(f == hex("0000006403000002030000002b000000040300000203000300000020feff"
                    "00500072006500760069006f007500730020007400690074006c00650000"));
    // captured: session 0x000b, model 0x03000001 (the phone's name)
    assert(textFrame(0x64, 1, "Pixel 9 Pro", f));
    assert(f == hex("0000006403000001030000002500000004030000010300030000001afeff"
                    "0050006900780065006c00200039002000500072006f0000"));
    // captured: session 0x003d, model 0x03000006
    assert(textFrame(0x64, 6, "Brighter", f));
    assert(f == hex("0000006403000006030000001f00000004030000060300030000" "0014feff"
                    "004200720069006700680074006500720000"));
    // captured: session 0x0023, a title with punctuation
    assert(textFrame(0x64, 5, "Bukkowasu!!", f));
    assert(f == hex("0000006403000005030000002500000004030000050300030000001afeff"
                    "00420075006b006b006f0077006100730075002100210000"));
    // non-ASCII goes out as UTF-16BE; the length byte counts BOM + text + NUL
    assert(textFrame(0x64, 9, "\xC3\x84" "b", f));          // "Äb"
    assert(f.size() == 28 + 2 + 4 + 2 && f[27] == 8 && f[30] == 0x00 && f[31] == 0xC4 && f[32] == 0x00 && f[33] == 'b');
    assert(kombinav::getU32(f, 9) == f.size() - 17);
    // limits: 24-bit index, one-byte text length (125 UTF-16 units)
    assert(!textFrame(0x64, 0x1000000u, "x", f));
    assert(textFrame(0x64, 2, std::string(125, 'a'), f));
    assert(!textFrame(0x64, 2, std::string(126, 'a'), f));
    // an empty text is still a valid model (BOM + NUL), as in the list frame's own trailing empty string
    assert(textFrame(0x64, 2, "", f) && f[27] == 4 && f.size() == 32);

    assert(commitFrame(0x64) == hex("00000064"));

    // SOME/IP header as iDrive sent it (client and session are ours)
    const Bytes m = someipRequest(kMethodCommit, commitFrame(0x64), 0x7E01, 0x0102);
    assert(m == hex("b00e00040000000c7e010102010100" "00" "00000064"));
    assert(replyProblem(hex("b00e0004000000087e01010201018000"), kMethodCommit, 0x7E01, 0x0102) == NULL);
    assert(replyProblem(hex("b00e0004000000087e01010201018001"), kMethodCommit, 0x7E01, 0x0102) != NULL);   // not E_OK
    assert(replyProblem(hex("b00e0004000000087e01010201018100"), kMethodCommit, 0x7E01, 0x0102) != NULL);   // ERROR
    assert(replyProblem(hex("b00e0003000000087e01010201018000"), kMethodCommit, 0x7E01, 0x0102) != NULL);   // other method
    assert(replyProblem(hex("b00e0004000000087e01010301018000"), kMethodCommit, 0x7E01, 0x0102) != NULL);   // other session
    assert(replyProblem(hex("b0060004000000087e01010201018000"), kMethodCommit, 0x7E01, 0x0102) != NULL);   // nav service
    assert(replyProblem(hex("b00e0004"), kMethodCommit, 0x7E01, 0x0102) != NULL);

    // ---- the track list as iDrive sent it (capture session 0x000f) ---------------------------------------------
    {
        const Bytes list = hex("000000640000006b300000005b0000000b0000006b300000000000030002000300020700001d0000002a"
                               "0000000a070000050103000002010000000affffffff0103000003000000000a07000006010300000401"
                               "0a00000003000001000000700000007100000004feff0000");
        assert(list.size() == 108);
        size_t at = 0;
        assert(trackListShape(list, &at) && at == 65 && kombinav::getU32(list, at) == 0x03000003);
        assert(trackListShape(list, NULL));
        Bytes b = list;
        b.pop_back();                                   // cut short: the length field no longer matches
        assert(!trackListShape(b, &at));
        b = list; b[7] = 0x6C;                          // another list model
        assert(!trackListShape(b, &at));
        b = list; b[17 + 10] = 4;                       // four rows
        assert(!trackListShape(b, &at));
        b = list; b[17 + 25 + 14 + 4] = 0x07;           // the middle row has an icon
        assert(!trackListShape(b, &at));
        b = list; b[17 + 25 + 28 + 7] = 0x09;           // the last row's icon is not "next"
        assert(!trackListShape(b, &at));
        b = list; b[65] = 0x07;                         // the middle row's text is not a text model
        assert(!trackListShape(b, &at));
        assert(!trackListShape(Bytes(), &at) && !trackListShape(Bytes(40, 0), &at));
        uint32_t v = 0;
        assert(shownValue(hex("00000064000000652e0000000900000003000000652e0000006b"), &v) && v == kListModel);
        assert(shownValue(hex("00000064000000652e0000000900000003000000652effffffff"), &v) && v == 0xFFFFFFFFu);
        assert(!shownValue(hex("00000064000000702c0000000800000002000000702c00ffff"), &v));
        assert(!shownValue(hex("00000064"), &v));
    }
    assert(methodAllowed(0x0003) && methodAllowed(0x0004) && methodAllowed(0x0009) && methodAllowed(0x000B));
    assert(!methodAllowed(0x0001) && !methodAllowed(0x0002) && !methodAllowed(0x0005));
    printf("test_ric_list OK\n");
    return 0;
}
