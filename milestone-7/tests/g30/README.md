# G30 PiP regression tests

From `milestone-7`:

```sh
make check-g30
```

The existing `make check` also runs this suite. Use `HOST_CXX=clang++` to
select Clang. Dependencies are Python 3, a C++11 compiler with AddressSanitizer
and UndefinedBehaviorSanitizer, FFmpeg with libx264, and OpenSSL headers
discoverable through pkg-config. Set `FFMPEG` to an executable path and
`OPENSSL_CFLAGS` to include flags when they are not on the normal search paths.
The full upstream test suite retains its existing Linux/ThreadSanitizer
requirements; `check-g30` can also run on macOS.

Tests that replace QNX sockets/time/Screen explicitly select
`NBT_G30_HOST_TEST` and add this test directory to the include path. Their
`qnx_api_stub.hpp` is test-only and rejects QNX targets. Production uses the
SDK directly without a custom SDK-selection flag. The player test omits the
production entry point and filesystem ownership marker; native trials cover
those paths. No SDK-free production build is retained in this source tree.

The eleven programs cover:

- `router` and `router_faults`: independent channel routing, live focus,
  decoder disconnect/reconnect, 32 rapid off/on/off cycles, keyframe recovery,
  malformed input, and stale credits after stream replacement.
- `encoded_queue` and `live_setting`: bounded RAM buffering, wraparound,
  capacity reuse, preference persistence and invalid setting files.
- `screen`, `control` and `pixels`: native resource/metadata bytes, secondary
  display ownership, buffer bounds, cleanup, real H.264 decoding and pixel
  placement. Unchanged images and coalesced predictive frames are covered.
- `control_recovery`: delayed and fragmented replies, a handle acquired after
  timeout, interruption, missing peers, wrong transaction IDs, negative replies
  and partial writes. Cleanup must consume the exact pending reply before it
  sends the unchanged factory cancellation sequence; a broken stream fails closed.
- `player_lifecycle`: the actual `player.cpp` entry point and OpenH264 decoder
  against simulated sockets and Screen. Cases include EOF, hangup, truncated
  headers/payloads, partial-read timeout, oversized payloads, stale stream
  generations, a damaged H.264 slice, partial Guidance acquisition, Screen
  allocation/post failures, startup lasting more than ten seconds, two slow
  activations in one connection, SIGTERM during Screen posting, the standalone
  65-second limit, and a successful restart. Each acquired socket,
  subscription, Guidance handle and Screen object must be released.
- `source-session`: actual AASession parsing, fragmented packets and deferred
  acknowledgements, with a scripted phone and plaintext test TLS. A stalled
  cluster consumer must leave the main dashboard stream responsive.
- `service_log`: live prefixed diagnostics, fragmented and oversized lines,
  final partial output, changing the daemon's log destination during playback,
  and bounded shutdown when a child retains the pipe.

Inputs are synthetic videos generated locally. Binaries, fixtures and logs go
to the ignored `tests/build/g30/` directory; `result.json` is written only after
the full suite passes. The tests require no firmware, SDK, simulator, phone or
vehicle. Native graphics/driver behavior and QNX process startup still require
separate native validation. Car installation tooling stays outside this repository.

The native supervisor regression runs the actual C++ process/address owner
against isolated filesystem and command fixtures. It covers other firmware
versions, normal stop, daemon death, stuck children, failed startup, retained
failure diagnostics, loss of the log reader, address/ARP/lock conflicts, display managers, factory
window preservation and failed alias cleanup. It never accesses a vehicle.

`qnx_spawn_probe.cpp` is an optional SDK-side process test. Compile on an
isolated QNX 6.5 x86 SDK host with `qcc -lang-c++ -march=i686 -D_QNX_SOURCE
-Wc,-std=gnu++0x -Isrc tests/g30/qnx_spawn_probe.cpp -o /absolute/path/probe`
and run the resulting absolute path. It checks worker-thread spawning,
descriptor isolation, signal reset, owner PID, and both prefixed output
streams. Its x86 GCC 4.4 atomic compatibility shim is test-only; production
ARM atomics are exercised separately by the native NBT simulator.
