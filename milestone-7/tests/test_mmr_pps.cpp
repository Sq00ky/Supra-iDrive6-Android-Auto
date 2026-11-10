// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Host test: mm-renderer PPS object parsing (src/mmr_pps.hpp) against the factory CarPlay context of a Supra
// (cpvideoctx16838, captured right after boot on 2026-10-27).
#include "mmr_pps.hpp"
#include "nbt_env.hpp"
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
    // v1.9.50-cpdetach: snapshot of the factory context, rebuilt on hand-back
    assert(ppsOutputId("output0") == 0 && ppsOutputId("output12") == 12);
    assert(ppsOutputId("output") == -1 && ppsOutputId("outputx") == -1 && ppsOutputId("input") == -1 && ppsOutputId(NULL) == -1);
    std::vector<PpsAttr> s = ppsSettings(o);
    assert(s.size() == 3 && s[0].name == "mmf.SCREEN_WRITER_DISABLE_CADENCE" && s[2].name == "mmf.SCREEN_WRITER_POST_BUFFER_NOSYNC");
    MmrCtxSnapshot snap;
    assert(!snap.hasOutput() && !snap.hasInput());
    snap.add("output1", "@output1\ntype::video\nurl::screen:/any?winid=9\n");
    snap.add("output0", output0);
    snap.add("param", "[n]@param\nprefetch_mode::0\n");
    snap.add("state", "@state\nstate::playing\n");
    assert(snap.outputIds.size() == 2 && snap.outputIds[0] == 0 && snap.outputIds[1] == 1);
    assert(ppsGet(snap.outputs[0], "url").find("wingrp=cpvideogrp16838") != std::string::npos);
    assert(ppsGet(snap.outputs[1], "url") == "screen:/any?winid=9");
    assert(snap.hasOutput() && !snap.hasInput() && ppsGet(snap.param, "prefetch_mode") == "0");
    snap.add("input", "@input\ntype::track\nurl::mdevpush:/dev/name/local/mdev?fourcc=AVC1\n");
    assert(snap.hasInput() && ppsGet(snap.input, "type") == "track");
    MmrCtxSnapshot empty;
    empty.add("output0", "@output0\n");     // a detached context: object without a url
    assert(!empty.hasOutput() && empty.outputIds.size() == 1);
    snap.clear();
    assert(!snap.hasOutput() && snap.outputIds.empty());
    // v1.9.57-cpcompat: an explicit env value wins over the CarPlay compatibility checkbox, junk / unset keeps it
    assert(settingOr(NULL, true) && !settingOr(NULL, false) && settingOr("", true) && !settingOr("", false));
    assert(settingOr("1", false) && settingOr("on", false) && settingOr("yes", false) && settingOr("true", false));
    assert(!settingOr("0", true) && !settingOr("off", true) && !settingOr("no", true) && !settingOr("false", true));
    assert(settingOr("maybe", true) && !settingOr("maybe", false));
    printf("test_mmr_pps OK\n");
    return 0;
}
