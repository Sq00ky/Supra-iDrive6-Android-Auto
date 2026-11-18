# Coding standards

Most of this describes how the existing code is written and what a change must keep doing. A few rules (marked in
the text) are stricter than the code is today; new code follows them, and old code is brought up to them only when it
is being changed anyway. When this file and the code disagree on a fact, the code wins: fix the file in the same PR.
Rules marked **must** are checked in review.

The binary runs inside a car. Treat every rule below as protecting two things: the iDrive screen must never break
because of us, and a developer with no car in front of them must be able to tell whether a change is right.

## 1. Language and compiler

The car builds with QNX SDP 6.5 `qcc` (GCC 4.4.2, ARMv7LE, `-Wc,-std=gnu++0x`). Host tests build with a modern
`g++ -std=gnu++0x -Wall`. Code must compile and behave the same on both.

- **Must** write C++03-style code. The existing code uses none of: `nullptr`, `auto`, lambdas, range-based `for`,
  `std::thread`, `std::atomic`, `std::function`, `std::shared_ptr`/`unique_ptr`, `std::to_string`.
- **Must not** use `__sync_*` or `__atomic_*` builtins (the car's libgcc has none). Use `pthread` mutexes; flags read
  by another thread are `volatile` and written under the documented rule for that module.
- Callbacks are a plain function pointer plus `void* user` (see `Link` in `cluster/j29_display.hpp`,
  `audio/backend.hpp`). Interfaces are small abstract classes.
- Include `<stddef.h>` for `size_t`. `size_t` is 32 bits on the car: check every length that could exceed 4 GiB or be
  attacker-controlled before it is used (the 32-bit `test_aa_proto` run exists for this).
- GCC 4.4.2 rejects some things newer compilers accept (for example a local type used as a template argument; see the
  note in `main.cpp`). If a host build passes and the car build fails, fix the code to the stricter reading.
- Do not add `-fPIC` to the daemon. The iDrive app and plugin are PIC shared objects built with the `_gpp` variant.
- No new third-party dependency without the maintainer's agreement. Vendored code stays under `third_party/` with its
  licence.

## 2. Files and licence header

Every new source file starts with:

```
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
```

followed by a short comment saying what the file is, what it deliberately does not do, and where its test is, for
example: `// Host-tested (tests/test_mfl.cpp). Plain C++98: the car builds with GCC 4.4.2.`

- Header guards are the path in upper case: `src/cluster/j29_graph.hpp` -> `CLUSTER_J29_GRAPH_HPP`.
- Code lives in `namespace nbt` (and a sub-namespace per area, such as `nbt::kombinav`). Nothing at global scope except
  `main` and C interop.
- **Pure logic goes in header-only, I/O-free units** (parsers, encoders, decisions, state machines). They take time and
  data as arguments and return results. That is what makes them testable on a laptop. Sockets, files, `/dev/*` and
  threads live in thin `.cpp` files that call the pure logic. If you are about to put `printf`-plus-decision-plus-socket
  into one function, split it first.
- One responsibility per file. A file that needs a section comment to explain what it holds wants splitting.
- Keep files and functions small enough to read in one go. When a function needs a comment saying "step 3 of 7", make
  the steps functions.

## 3. Style

Match the surrounding code. Do not reformat lines you are not changing.

- 4 spaces, no tabs. Opening brace on the same line (`if (x) {`, `} else if (y) {`). A one-line `if (x) return;` is
  fine when it stays short.
- Keep lines to about 120 columns. Comments wrap at the same width.
- Names: types and classes `PascalCase`; functions and methods `camelCase`; members end in `_` (`probe_`,
  `inplace_ok_`); constants and enumerators start with `k` (`kApproachTime`, `kRejected`); macros and env variables
  `UPPER_SNAKE`; test files `test_<topic>.cpp`.
- Prefer `const` and `const&`; no non-const globals. File-scope `static` or an anonymous namespace for helpers.
- One declaration per line. Initialise everything, including struct members (a constructor initialiser list, as in
  `Settings`).
- No magic numbers: a named constant with a comment saying where the number comes from (spec, capture, measurement).
- No dead code, no commented-out code, no `TODO` without a reason and an issue number.

## 4. Comments

Comments explain **why**, and record facts the code cannot show. The code says what.

- Protocol and car facts carry their evidence: `// car-verified 2026-11-01: lane row on cluster and HUD`, or
  `// not car-verified: follows the enum names`.
- Say what a value or field means and its unit (`metres`, `seconds`, `percent`, `0 = off`).
- Explain every workaround for a compiler or a firmware quirk, naming the quirk.
- Do not restate the next line in words. Do not leave a comment that has stopped being true: update it in the same
  change.

## 5. Errors and robustness

- No exceptions in new code. Report failure with a return value or a small result enum (`ClusterDisplay::Result`:
  `kOk`, `kLink`, `kRejected`, `kInternal`) and let the caller decide. Check every return value that can fail.
- **Everything that arrives from outside is untrusted**: phone messages, car (DSI/MOST) frames, files in `/var` and
  `/opt`, environment variables, the cluster's answers. Bounds-check before reading, validate lengths against the
  buffer, and reject rather than guess. Malformed input must produce a logged refusal, never a crash or a read past
  the end.
- Degrade, do not die. If the optional feature fails (cluster refuses, a file is missing), log once, switch that
  feature off, and keep the rest of the session running. Never take down iDrive, the screen, or the phone session for
  a cosmetic feature.
- Bound everything that grows: queues, strings (`utf8Prefix` style limits), retry counts, log lines per second.
- Long-running means leaks matter. Every `new` has an owner, every file descriptor, socket and thread has a defined
  close/join on every path including errors. Do not destroy an object another thread still uses.
- Time is a parameter (`double now`), not a call to `time()` inside logic, so tests can drive it.

## 6. Threads

- Use `pthread` mutexes. Keep each lock's scope small, never call into unknown code (callbacks, I/O) while holding one
  unless the module's header documents it.
- Document which thread owns each piece of state in the header, next to the member.
- A change that touches threads, locks or shutdown must be run under the ThreadSanitizer build (`make -C tests tsan`)
  and, if it adds a thread, have a lifecycle test (start, work, stop, repeat, as `test_session_lifecycle` does).

## 7. Logging

- Format: `printf("[Module] message\n", ...)`, one event per line, module name in brackets (`[KombiNav]`,
  `[SDR]`, `[MOSTLOG]`). The text must be greppable and understandable without the source.
- Log state changes and decisions, not every cycle. Repeated conditions are rate-limited or logged once ("not logged
  again this session"). A new rule: nothing logs per video or audio frame by default.
- The effective value of any new setting is logged once at start, including where it came from.
- **Never log secrets or personal data**: Wi-Fi or Wi-Fi Direct passphrases, tokens, keys, phone MACs, VINs. Route
  text and positions are logged only where a feature needs them for diagnosis, and a log that people share is
  expected to be stripped first (say so in the readme when a new log line carries such data).
- A new log line a user will be asked to send is documented in the readme, so support questions can ask for it by name.

## 8. Configuration and feature flags

- Settings are `NBT_*` environment variables read from `/opt/aa/nbt-aad.env`, parsed with the existing helpers
  (`envFlag`, `envNumber` in `cluster/nav_core.cpp`; `nbt_env.hpp`) and not with ad-hoc `getenv` plus `atoi`. A value
  that is not a whole number in its range is ignored with a log note and the default stays; it must not abort.
- **A new setting's default must leave current behaviour unchanged**, and a test proves that.
- A setting that exists for car testing (a probe, a sweep, a forced mode) is off by default, logs loudly when on, and
  is documented as parked-only.
- Document every setting in the readme's configuration table in the same PR: name, values, default, what it changes.
- Hardware-specific behaviour (a cluster, a HUD, a car generation) goes behind its own driver or setting
  (`ClusterDisplay`, `KombiNav::createDisplay`), not as `if (this_car)` branches inside shared code. Adding a cluster
  adds a driver; it does not edit another cluster's driver.

## 9. Protocol and replayed data

- Where the code replays a capture (the J29 route graph template, lane models), change only the fields that are
  proven on a car, and keep the rest byte for byte. The golden transcript test (`test_j29_golden`) must stay green; a
  deliberate change to it is explained in the PR.
- Decoders are written against real captures in `tests/fixtures/` and fail closed on anything they do not recognise.
- Do not copy enum values from another project into the code as facts. Cite them as "from <project>, not car-verified"
  until a car confirms them.

## 10. Security and privacy

- No credentials, keys or tokens in the repo. Test values are obviously fake.
- Fixtures taken from a car are stripped of positions, street names, MACs, VINs, Wi-Fi names and passphrases before
  they are committed.
- The daemon has wide access to the head unit and the cluster network: do not add network listeners, shell-outs with
  user-influenced strings, or writes outside the paths the feature documents. Build commands from fixed strings and
  check every path component you take from outside.
- Anything that writes to the car's flash (not `/dev/shmem`) is documented, off by default, and reversible.

## 11. Before you open the PR

- [ ] The change has tests (docs/TESTING.md) and `make -C tests check` ends with `[CHECK] all host tests passed`.
- [ ] No new compiler warnings; nothing uses a C++11 feature the car's compiler lacks.
- [ ] New files have the licence header and a one-paragraph description of the file.
- [ ] New or changed settings, log lines, tests and behaviour are in the readme.
- [ ] `NBT_AAD_BUILD` is raised if the daemon's behaviour changed.
- [ ] No private data in code, fixtures, tests or the PR text.
- [ ] The PR says what was verified on a car, what was only host-tested, and what is assumed.
