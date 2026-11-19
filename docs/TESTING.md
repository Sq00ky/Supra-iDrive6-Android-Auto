# Testing

Nobody can try a change on a car from their desk, so the host tests are the safety net. They run on any Linux `g++`
(WSL and a Linux container both work) and finish in about a minute. **A change without a test does not merge**, with
one exception: something that truly cannot be reached on the host (the car's screen, a real cluster). Then the PR
says so, and still tests every piece of logic around it.

## Running them

From `milestone-7/`:

```sh
make -C tests check            # build and run every test, then test_session_lifecycle under ThreadSanitizer
make -C tests run-test_NAME    # build and run one test (binary and log in tests/build/)
make -C tests tsan             # only the ThreadSanitizer run
```

Needs `g++`, OpenSSL dev headers, `make` and `python3`. A good run ends with `[CHECK] all host tests passed`.

Two lines may print `[SKIP]` on a normal machine: `test_aa_proto (32-bit)` (no `-m32` toolchain) and
`test_schema.py` (no `protoc`). Report any other skip or any `[FAIL]` as it is. If you cannot run the suite at all,
say that in the PR; do not write "tests pass".

Tests run with the working directory set to `milestone-7`, so fixture paths are relative to it
(`tests/fixtures/...`). Fixtures are LF. `tests/fixtures/j29_golden.txt` is pinned to LF in `.gitattributes` because
it is byte-exact; if a CRLF checkout breaks a fixture, convert a copy, never commit CRLF.

## What every change must cover

For each change, write tests for the ones that apply, and say in the PR which you chose not to and why.

1. **The new behaviour**, with the exact expected output for given inputs.
2. **Boundaries**: empty, one, maximum, one past the maximum; zero and negative numbers; the first and last item.
3. **Bad input**: truncated or oversized frames, wrong lengths, values out of range, text that is not valid UTF-8,
   a peer that refuses or goes silent. The expectation is a clean refusal and a log line, never a crash.
4. **The default is unchanged**: with a new setting off, the old behaviour is identical (the existing transcripts and
   assertions are the proof; add one that names it).
5. **A regression test for every bug fix.** Write it first and watch it fail for the reason in the bug report. A test
   that passes before the fix proves nothing.
6. **Time and order**: anything with timers, retries, grace periods or debouncing is tested by passing the time in,
   including the moment just before and just after each limit.
7. **Concurrency**: shared state or a new thread gets a lifecycle test and a ThreadSanitizer run.
8. **32-bit**: length arithmetic that could overflow a 32-bit `size_t` gets a case in the 32-bit run (`test_aa_proto`).

## Writing a test

A test is `milestone-7/tests/test_<topic>.cpp` with a `main()` that returns 0 on success.

1. Write the test file. Use plain `assert()` or a local `CHECK(cond)` macro that counts failures and prints
   `FAIL file:line expression`; both styles exist. Do not define `NDEBUG`: asserts must stay on. End a passing run by
   printing `test_<topic>: ok` (or `all checks passed`).
2. Register it in `tests/Makefile` with a line naming the daemon sources it links beyond its own file:
   `SRC_test_<topic> := src/cluster/j29_display.cpp ...` (empty value for header-only code). The list of tests is
   built from those `SRC_test_*` variables, so a test with no line is never built or run. If the test needs a path
   argument, add `ARGS_test_<topic> := tests/fixtures/<file>`.
3. If it links the whole guidance runtime, reuse `$(SRC_KOMBI_NAV)` as the existing KOMBI tests do.
4. Add a row to the readme's test table (`milestone-7/readme.md`, "Building and tests"): test name and what it covers.
5. Run `make -C tests run-test_<topic>`, then the whole suite.

## How to make code testable

- **Pure logic, header-only, no I/O.** Decisions take their inputs as arguments (including `double now`) and return a
  result. Put sockets, files and threads in thin `.cpp` files. See `car/mfl.hpp` and `tests/test_mfl.cpp` for a small
  complete example, and `cluster/nav_core.*` with `tests/test_kombi_nav.cpp` for a larger one.
- **Fakes over mocks of internals.** The tests already have fakes: `fake_kombi.hpp` (a loopback SOME/IP server that
  records requests and can refuse chosen ones), `fake_dsi_client.*` (car frames in, events out),
  `kombi_nav_test_util.hpp` (a protobuf encoder and a scripted drive), `stub_cryptor.hpp`. Extend a fake before you
  write a second one. A callback struct (`Link`, `fn + void* user`) is the usual seam: a test passes its own
  functions that record what would have gone to the car.
- **Prefer no sockets.** When a real socket is needed, bind a loopback port chosen by the OS and clean it up on every
  path. No test touches `/dev`, `/var`, `/opt` or the real network.
- **No `sleep` to wait for something.** Pass time in, or wait on a condition with a hard timeout that fails loudly.
  A test that is flaky is a failing test: fix it or delete it, do not retry it.
- **Deterministic**: no dependence on the clock, the host's locale, environment variables left over from another test,
  or test order. Set and restore environment variables you use.
- **Test behaviour, not implementation.** Assert on what leaves the code (frames sent, text shown, state reached),
  not on private members. A refactor that keeps behaviour must not break a test.
- **Real structure, invented values.** Base a test on what a real car or phone actually sends (field order, lengths,
  message types), so decoders are tested against the real shape. Fill every value that could identify a person or a
  place with an invented one (see "Fixtures and golden transcripts"). Never commit a raw capture, and do not hand-type
  a layout you guessed: say which capture the shape was taken from (car, date, build), not what was in it.

## Fixtures and golden transcripts

- A fixture is modelled on a real capture but holds **invented values only**. Real car data can carry personal
  information that is not obvious (positions, street names, phone MACs, VINs, Wi-Fi names and passphrases, timestamps,
  device identifiers, free text), and stripping known fields by hand will miss some. So do not "clean" a capture and
  commit it: rebuild the message with made-up values (fake coordinates, a placeholder street, an obviously fake MAC)
  and keep only the structure. If a test needs a byte-exact value that you cannot invent, ask the maintainer rather
  than committing it.
- Fixtures already in the tree predate this rule. Do not copy values out of them into new places. If you find a real
  value in one, tell the maintainer rather than quietly editing it.
- `test_j29_golden` pins every byte the daemon sends for a scripted drive. A change that alters the transcript is a
  behaviour change: update the transcript on purpose, in its own commit, and explain in the PR what changed on the
  wire and why it is safe on the cluster. Never regenerate a golden file just to make a failure go away.

## Reviewing tests

A reviewer (or you, before pushing) should be able to answer yes to all of these:

- Would this test fail if the feature were broken or the bug came back?
- Is every assertion about something that matters, and does each failure message say what went wrong?
- Does it run in a second or two, with no network, no real time and no files outside `tests/`?
- Is it listed in `tests/Makefile` and in the readme's test table?
- Did I run the whole suite, not only this test?

## What the tests do not prove

Passing host tests do not prove a change works in the car: the cluster, the iDrive screen, the phone and the
firmware are not in them. For anything that touches those, the PR states what was run on a car (build string, car,
cluster, what was seen) and what was not. Mark anything unseen "not car-verified" in the code and the readme.
