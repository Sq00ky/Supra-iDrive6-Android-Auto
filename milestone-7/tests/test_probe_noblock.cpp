// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Host check (#364): the default control-port probe must never wait; nothing listens on 52790 here.
// Not in `make check`: it #includes the plugin source, whose bmw:: framework symbols stay unresolved. From milestone-7:
//   macOS: clang++ -std=gnu++98 -w tests/test_probe_noblock.cpp -o /tmp/probe -Wl,-undefined,dynamic_lookup && /tmp/probe
//   Linux: g++ -std=gnu++98 -w tests/test_probe_noblock.cpp -o /tmp/probe -Wl,--unresolved-symbols=ignore-all && /tmp/probe
// The previous controlPortBusy() fails the first assert (it waited 200 ms).
#include <string>
#include "../rhmiapp/nbt_aad_app.cpp"
#include <assert.h>
#include <stdio.h>
int main() {
    for (int i = 0; i < 5; ++i) {
        uint32_t t0 = nowMs();
        bool busy = controlPortBusy();
        uint32_t dt = nowMs() - t0;
        assert(!busy && dt < 20);
        t0 = nowMs();
        DaemonState d = readDaemonState();               // no state file on the host: the probe path
        dt = nowMs() - t0;
        assert(d.pid == 0 && dt < 20);
    }
    uint32_t t0 = nowMs();
    assert(!controlPortBusy(true));                      // driver-action path still waits for an answer
    assert(nowMs() - t0 >= 150);
    puts("probe ok");
    return 0;
}
