// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Host test: mm-renderer PPS object parsing (src/mmr_pps.hpp) against the factory CarPlay context of a Supra
// (cpvideoctx16838, captured right after boot on 2026-10-27).
#include "mmr_pps.hpp"
#include <assert.h>
#include <stdio.h>
using namespace nbt;

int main() {
    const char* output0 =
        "[n]@output0\n"
        "mmf.SCREEN_WRITER_DISABLE_CADENCE::1\n"
        "mmf.SCREEN_WRITER_DISABLE_MEDIACLOCK::1\n"
        "mmf.SCREEN_WRITER_POST_BUFFER_NOSYNC::1\n"
        "type::video\n"
        "url::screen:/any?winid=1280&nodstviewport=0&dstx=0&dsty=0&dstw=1280&dsth=480&wingrp=cpvideogrp16838\n";
    std::vector<PpsAttr> o = ppsParse(output0);
    assert(o.size() == 5);
    assert(ppsGet(o, "type") == "video");
    assert(ppsGet(o, "url") == "screen:/any?winid=1280&nodstviewport=0&dstx=0&dsty=0&dstw=1280&dsth=480&wingrp=cpvideogrp16838");
    assert(o[0].name == "mmf.SCREEN_WRITER_DISABLE_CADENCE" && o[0].value == "1");
    std::vector<PpsAttr> p = ppsParse("[n]@param\r\nprefetch_mode::0\r\n");
    assert(p.size() == 1 && p[0].name == "prefetch_mode" && p[0].value == "0");
    std::vector<PpsAttr> e = ppsParse("@state\nstate:n:2\nbroken line\n:x:y\n");
    assert(e.size() == 1 && ppsGet(e, "state") == "2" && ppsGet(e, "missing", "d") == "d");
    printf("test_mmr_pps OK\n");
    return 0;
}
