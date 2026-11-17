# G30 PiP regression tests

From `milestone-7`:

```sh
make check-cluster-video
```

The existing `make check` also runs this suite. Use `HOST_CXX=clang++` to
select Clang. Dependencies are Python 3, a C++11 compiler with AddressSanitizer
and UndefinedBehaviorSanitizer, FFmpeg with libx264, and OpenSSL headers
discoverable through pkg-config. Set `FFMPEG` to an executable path and
`OPENSSL_CFLAGS` to include flags when they are not on the normal search paths.
The full upstream test suite retains its existing Linux/ThreadSanitizer
requirements; `check-cluster-video` can also run on macOS.

Tests that replace QNX sockets/time/Screen explicitly select
`NBT_CLUSTER_HOST_TEST` and add this test directory to the include path. Their
`qnx_api_stub.hpp` is test-only and rejects QNX targets. Production uses the
SDK directly without a custom SDK-selection flag. The player test omits the
production entry point and filesystem ownership marker; native trials cover
those paths. No SDK-free production build is retained in this source tree.

The runner checks both build profiles, then runs the C++ regression programs:

- `profiles` (compiled for both J29 and G30): default Guidance selection,
  shared reference graph, and the G30 output/resource contract.
- `hardware_decoder`: private renderer ownership, native packet framing,
  asynchronous last-frame delivery, padded-stride BGRA fitting, quiet maps,
  84 partial-startup failure points and first-output/readback timeouts. Its
  deterministic API peer supplies synthetic pictures; it does not emulate
  IVA-HD. The same test can also run under the native QNX SDK and ARM simulator.
  Padding coverage includes the car's 896×576 allocation and 800×480 viewport
  at (32,24), another valid offset, delayed viewport setup, invalid dimensions,
  overflow-edge bounds, and the native-copy pixmap usage. Every copied pixel
  and the complete fitted image must match the unpadded reference.
- `router` and `router_faults`: independent channel routing, live focus,
  decoder disconnect/reconnect, 32 rapid off/on/off cycles, keyframe recovery,
  malformed input, and stale credits after stream replacement.
- `recovery` and `player_service`: bounded retry backoff, clock wraparound,
  progress/idle deadlines, real helper exit and restart, stopping before
  replacement, disabling during backoff, and latched ownership/preflight errors.
- `manifest`: exact cluster padding changes against the DHU discovery fixture,
  byte-for-byte preservation of other fields and channels, idempotence, buffer
  bounds, missing/duplicate cluster channels and malformed protobuf fields.
- `encoded_queue` and `live_setting`: bounded RAM buffering, wraparound,
  capacity reuse, preference persistence, invalid setting files and precedence
  of generic environment options over legacy G30 names.
- `screen`, `control` and `pixels`: native resource/metadata bytes, secondary
  display ownership, buffer bounds, cleanup and placement of synthetic renderer pictures in the PiP backing.
  The hardware decoder and capture tests cover duplicate and coalesced pictures.
- `control_recovery`: delayed and fragmented replies, a handle acquired after
  timeout, interruption, missing peers, wrong transaction IDs, negative replies
  and partial writes. Cleanup must consume the exact pending reply before it
  sends the unchanged factory cancellation sequence; a broken stream fails closed.
- `player_lifecycle`: the `player.cpp` loop and production hardware session
  against controlled renderer, socket and Screen peers. Cases include EOF, hangup, truncated
  headers/payloads, partial-read timeout, oversized payloads, stale stream
  generations, missing initial SPS/PPS, partial Guidance acquisition, Screen
  allocation/post failures, startup lasting more than ten seconds, two slow
  activations in one connection, SIGTERM during Screen posting, the standalone
  65-second limit, and a successful restart. Each acquired socket,
  subscription, Guidance handle and Screen object must be released.
  HUD selection is exercised twice in one player: Guidance/Screen release,
  retained sink subscription, cached-picture resume, and map-off while paused.
  The captured 36,832-of-42,919-byte access-unit stall is also reproduced.
- `source-session`: actual AASession parsing, fragmented packets and deferred
  acknowledgements, with a scripted phone and plaintext test TLS. A stalled
  cluster consumer must leave the main dashboard stream responsive.
- `service_log`: live prefixed diagnostics, fragmented and oversized lines,
  final partial output, changing the daemon's log destination during playback,
  and bounded shutdown when a child retains the pipe.

The runner generates a moving H.264 fixture and runs the sanitized C++
programs and native supervisor cases.
Each build/run stage writes its own log, and failures include the log tail.
`make_fixture.py` places thirty synthetic access units between ACP1 reset and
stop packets; its timestamps advance by 100 ms.

Binaries, fixtures and logs go to the ignored `tests/build/cluster_video/` directory. A
previous `result.json` is removed at startup, and a new one is written only
after the full suite passes. The tests require no firmware, SDK, simulator, phone or
vehicle. Native graphics/driver behavior and QNX process startup still require
separate native validation. Car installation tooling stays outside this repository.

The native supervisor regression runs the actual C++ process/address owner
against isolated filesystem and command fixtures. It covers other firmware
versions, normal stop, daemon death, stuck children, failed startup, retained
failure diagnostics, loss of the log reader, address/ARP/lock conflicts, display managers,
factory window preservation and failed alias cleanup. It never accesses a vehicle.
The 17 supervisor cases include exact reaped-child renderer cleanup and verify
that failed cleanup retains the lease and address.

`qnx_spawn_probe.cpp` is an optional SDK-side process test. Compile on an
isolated QNX 6.5 x86 SDK host with `qcc -lang-c++ -march=i686 -D_QNX_SOURCE
-Wc,-std=gnu++0x -Isrc tests/cluster_video/qnx_spawn_probe.cpp -o /absolute/path/probe`
and run the resulting absolute path. It checks worker-thread spawning,
descriptor isolation, signal reset, owner PID, and both prefixed output
streams. Its x86 GCC 4.4 atomic compatibility shim is test-only; production
ARM atomics are exercised separately by the native NBT simulator.

`qnx_hardware_timeout.cpp` tests the actual hardware adapter against QNX kernel
IPC in the isolated SDK VM. It reproduces the old immediate timeout, then checks
20 readback/following-IPC cycles, successful nonzero previous flags, failure
before readback IPC, saved errno and successful/failed encoded sends. Screen
readback is a callback; TimerTimeout and message passing are real kernel calls.
The old adapter must fail the first following IPC with ETIMEDOUT (260).

From `milestone-7` in that VM:

```sh
qcc -lang-c++ -Wc,-std=gnu++0x -DNBT_CLUSTER_PROFILE_G30 -Isrc \
  tests/cluster_video/qnx_hardware_timeout.cpp \
  src/cluster/video/hardware_decoder.cpp -lsocket -lm -o /dev/shmem/hw-timeout-test
/dev/shmem/hw-timeout-test
```

`test_hardware_capture.cpp` runs the production asynchronous session with real
threads, sockets and a controlled Screen peer. Readback stays blocked while
24 fragmented 87,039-byte access units reach mdev, byte-for-byte and in order,
within the three-second transport budget. It also models 350 ms readback,
newest-picture replacement, quiet input, reset, capture failure and shutdown
joining an in-flight copy before destroying its objects. The host suite uses
ASan/UBSan; this test can additionally run under ThreadSanitizer.

Run the same concurrency contract in the isolated QNX SDK VM with:

```sh
qcc -lang-c++ -Wc,-std=gnu++0x -O2 -DNBT_CLUSTER_PROFILE_G30 -Isrc \
  tests/cluster_video/test_hardware_capture.cpp -lsocket -lm \
  -o /dev/shmem/hw-capture-test
/dev/shmem/hw-capture-test
```

This verifies scheduling and ownership with real QNX pthreads/socket IPC;
the peer does not emulate IVA-HD or validate physical image quality.

`test_hardware_scaling.cpp` verifies the literal 522×313 pixmap request and every
packed pixel, including both 49-pixel sidebars, opaque alpha, odd source stride,
source/destination guards and rejection/cleanup of invalid size/stride/format.
`test_hardware_decoder.cpp` additionally checks padded 800×480 source viewports
and every scaled pixel/row boundary. The Screen peer uses an explicit sampling
rule only for tests; it does not predict the physical scaler's filtering.
Both tests also compile/run with the native QNX SDK like the capture test above.
