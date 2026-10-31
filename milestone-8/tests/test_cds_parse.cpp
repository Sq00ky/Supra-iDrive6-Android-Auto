// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Host test for rhmiapp/cds_parse.hpp with the callStatus texts from the car (nbt-aad-app-8.log, 2026-10-28).
#include "../rhmiapp/cds_parse.hpp"
#include <assert.h>
#include <stdio.h>

int main() {
    // line 0 during a call, then idle
    assert(nbt::cdsCallActive("{\"callStatus\":{\"id\":0,\"state\":3}}"));
    assert(nbt::cdsCallActive("{\"callStatus\":{\"id\":0,\"state\":2}}"));
    assert(!nbt::cdsCallActive("{\"callStatus\":{\"id\":0,\"state\":0}}"));
    // line 1 idle: the old parser read the id (1) and kept the app in "call in progress" forever
    assert(!nbt::cdsCallActive("{\"callStatus\":{\"id\":1,\"state\":0}}"));
    assert(nbt::cdsCallActive("{\"callStatus\":{\"id\":1,\"state\":1}}"));
    // unknown layout -> no call
    assert(!nbt::cdsCallActive("{\"callStatus\":{\"id\":1}}"));
    assert(!nbt::cdsCallActive(""));
    assert(!nbt::cdsCallActive(0));
    assert(nbt::cdsIntField("{\"callStatus\":{\"id\":1,\"state\":0}}", "id", -1) == 1);
    assert(nbt::cdsIntField("{\"a\": 7}", "a", -1) == 7);
    assert(nbt::cdsIntField("{\"a\":x}", "a", -1) == -1);
    // vehicle.dayNightChangeOver (libhmiremoting format '{"dayNightChangeOver":%u}')
    assert(nbt::cdsIntField("{\"dayNightChangeOver\":2}", "dayNightChangeOver", -1) == 2);
    assert(nbt::cdsIntField("{\"dayNightChangeOver\":1}", "dayNightChangeOver", -1) == 1);
    assert(nbt::cdsIntField("{}", "dayNightChangeOver", -1) == -1);
    printf("test_cds_parse OK\n");
    return 0;
}
