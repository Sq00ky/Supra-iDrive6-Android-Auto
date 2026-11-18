# CLAUDE.md - rules for every AI coding session in this repo

This file is read automatically by Claude Code. Other AI tools: see `AGENTS.md`. Humans: these are also the rules a
pull request is reviewed against.

The code runs on a car's head unit (BMW NBT Evo, QNX 6.5, GCC 4.4.2, ARMv7). A bug here is not a failed web request:
it can blank the iDrive screen or leave the cluster showing nothing. There is no way to hot-fix a car. So the bar is
"tested before it ships", not "looks right".

Details live in two files. Read them before writing code:

- `docs/CODING-STANDARDS.md` - language subset, layout, naming, errors, logging, configuration, threads.
- `docs/TESTING.md` - how to write, register and run tests; what every change must cover.

## Hard rules (a change that breaks one is not done)

1. **No code change without a test.** Every new behaviour and every bug fix gets a host test in
   `milestone-7/tests/`. A bug fix needs a regression test that fails without the fix. If a change truly cannot be
   tested on the host, say why in the PR and test the logic you can reach. Never delete, loosen or skip an existing
   test to make a change pass: fix the code, or explain in the PR why the test's expectation was wrong.
2. **Run the whole host suite before you say you are done**, and report the real result (see "Run the tests"). A
   partial run is not "tests pass". If something fails, say so with the output.
3. **Write for the car's compiler.** GCC 4.4.2: C++03 style, no `nullptr`, `auto`, lambdas, range-for, `std::thread`,
   `std::atomic`, `std::function`, smart pointers or `__sync_*`/`__atomic_*`. Use pthread mutexes and `fn + void*`
   callbacks like the existing code. Host builds use `-std=gnu++0x -Wall`: do not add warnings.
4. **Do not invent protocol or car facts.** A value is "car-verified" only when someone saw it on a car, and the
   source (capture, photo, date) is in the comment or readme. Anything else is labelled "not car-verified". Unknown
   stays unknown; do not fill gaps with plausible numbers.
5. **Do not change shared behaviour for one car.** Put a cluster, HUD or car-specific change behind its own driver or
   setting (`ClusterDisplay` / `KombiNav::createDisplay`, an `NBT_*` env var) with the default leaving today's
   behaviour unchanged, and add a test that proves the default is unchanged.
6. **Experiments are off by default and say so.** Anything that sends test values to a car (probes, sweeps) needs an
   explicit opt-in env var, a loud log line when it is on, and a note that it is for a parked car only.
7. **Keep private data out of the repo and out of logs.** No GPS positions, phone MACs, VINs, Wi-Fi names or
   passphrases, route street names, tokens or keys in code, fixtures, tests, commit messages, issues or PRs. Never
   log a passphrase. Captures used as fixtures are stripped first.
8. **Do not commit proprietary material**: QNX SDK or licence files, firmware dumps, BMW binaries, libraries copied
   from a car. Do not help anyone obtain unlicensed toolchains.
9. **New files carry the licence header** (see `docs/CODING-STANDARDS.md`). Do not add third-party code unless its
   licence is compatible with PolyForm Noncommercial 1.0.0, and credit its source and licence in the file header.
10. **Never give deployment steps that copy over a running binary.** The old file is moved aside first
    (`mv nbt-aad nbt-aad.old`), as `updating.md` does, so a bad build can be undone.

## Process for every change

1. Branch from the latest `main` (`git fetch`, then branch). Never commit to `main`. One topic per branch and PR.
2. Read the code you are about to change and its tests first. Match the existing style; do not reformat unrelated code.
3. Write or extend the test first, watch it fail for the right reason, then write the code.
4. Keep the diff small and reviewable. No drive-by refactors, renames or whitespace changes in the same PR.
5. Run the full host suite. Fix warnings.
6. Update the docs the change touches (see "Documentation" below).
7. Commit only the files you changed, by name (never `git add -A`), with a message in the repo's style:
   `Area: what changed and why (vX.Y.Z-name)`. One logical change per commit.
8. Do not push, open a PR, comment on or close an issue, or post anywhere unless the user explicitly asks. Never
   force-push a shared branch.

## Run the tests

From `milestone-7/`, on Linux, WSL or a Linux container with `g++`, OpenSSL dev headers and `python3`:

```sh
make -C tests check          # every test, then test_session_lifecycle under ThreadSanitizer
make -C tests run-test_NAME  # one test
```

- Expected output ends with `[CHECK] all host tests passed`.
- Two lines may legitimately say `[SKIP]`: `test_aa_proto (32-bit)` (no `-m32` toolchain) and `test_schema.py`
  (no `protoc`). Any other skip or `[FAIL]` is a problem to report, not to hide.
- When CRLF checkouts break fixtures on Windows, convert fixtures to LF in a copy; never commit CRLF fixtures.
  `tests/fixtures/j29_golden.txt` is pinned to LF in `.gitattributes`.

## Documentation a change must update

- `milestone-7/readme.md`: a new or changed `NBT_*` variable goes in the configuration table; a behaviour change gets
  a short dated note in the style of the existing `> vX.Y.Z-name ...` entries, with "car-verified" or "not
  car-verified"; a new test goes in the test table; a new directory or module goes in "Source layout".
- `NBT_AAD_BUILD` in `milestone-7/src/main.cpp`: raise it for any change that alters what the daemon does
  (`vX.Y.Z-short-name`). The packaging workflow reads the `X.Y.Z` part and refuses a number that was used before
  (`tools/nbtpkg/README.md`). Test builds are published as pre-releases.
- The iDrive plugin (`milestone-7/rhmiapp/`) has its own `rhmiapp-NN-name` number; raise it when the plugin changes,
  and keep daemon and plugin in step (the menu pings the daemon).

## Honest reporting

When you finish, state exactly what you ran, what passed, what you could not run (anything that needs a car or the
QNX toolchain), and what you assumed. "Not tested on a car" is a normal, acceptable thing to write. A confident claim
that was never checked is not.

## Quick map

| Where | What |
|---|---|
| `milestone-7/src/` | daemon: `main.cpp`, session, audio, video, car input, `cluster/` (KOMBI and HUD drivers) |
| `milestone-7/src/cluster/` | `nav_core` (when to show what), `display.hpp` (the `ClusterDisplay` interface and `Settings`), `j29_*` (J29 cluster), `hud*` (6WA HUD) |
| `milestone-7/tests/` | host tests, fixtures, fakes (`fake_kombi.hpp`, `fake_dsi_client.*`) |
| `milestone-7/rhmiapp/` | the iDrive menu plugin |
| `tools/nbtpkg/` | release packaging and its tests |
| `build.sh`, `milestone-7/Makefile` | car build (QNX `qcc`) |
