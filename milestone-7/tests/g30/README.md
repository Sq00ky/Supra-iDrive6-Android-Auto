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

The nine programs cover:

- `router` and `router_faults`: independent channel routing, live focus,
  decoder disconnect/reconnect, 32 rapid off/on/off cycles, keyframe recovery,
  malformed input, and stale credits after stream replacement.
- `encoded_queue` and `live_setting`: bounded RAM buffering, wraparound,
  capacity reuse, preference persistence and invalid setting files.
- `screen`, `control` and `pixels`: native resource/metadata bytes, secondary
  display ownership, buffer bounds, cleanup, real H.264 decoding and pixel
  placement. Unchanged images and coalesced predictive frames are covered.
- `player_lifecycle`: the actual `player.cpp` entry point and OpenH264 decoder
  against simulated sockets and Screen. Cases include EOF, hangup, truncated
  headers/payloads, partial-read timeout, oversized payloads, stale stream
  generations, a damaged H.264 slice, partial Guidance acquisition, Screen
  allocation/post failures, and a successful restart. Each acquired socket,
  subscription, Guidance handle and Screen object must be released.
- `source-session`: actual AASession parsing, fragmented packets and deferred
  acknowledgements, with a scripted phone and plaintext test TLS. A stalled
  cluster consumer must leave the main dashboard stream responsive.

Inputs are synthetic videos generated locally. Binaries, fixtures and logs go
to the ignored `tests/build/g30/` directory; `result.json` is written only after
the full suite passes. The tests require no firmware, SDK, simulator, phone or
vehicle. Native graphics/driver behavior and QNX process startup still require
separate native validation. Deployment-supervisor tests stay with the external
deployment tooling.
