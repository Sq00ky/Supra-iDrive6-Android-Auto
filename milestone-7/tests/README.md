# Host tests

These tests use Linux compilers and fake car services. They do not need a QNX SDK, proprietary libraries,
credentials, a phone or a connection to a head unit. They do not validate QNX linkage or the picture on the real HUD.

## Linux and Apple Silicon with Docker

Install Docker Engine on Linux, or Docker Desktop on Apple Silicon, and start it. From the repository root:

```sh
./tools/test-host.sh
```

This is the Docker command CI runs; a second CI job tests the Ubuntu package recipe below directly.
It builds both stages of [Dockerfile](Dockerfile), then runs:

- The native Linux suite, including protobuf schema checks with `protoc`, resource/updater tests,
  the missing-tool regression tests and `test_session_lifecycle` under ThreadSanitizer.
- The mandatory 32-bit protobuf test in a `linux/amd64` Ubuntu image with GCC 13 multilib.
  The image also compiles and runs a 32-bit C++ program during its build.

On Apple Silicon the first suite runs on ARM64, so TSAN is not emulated. Only the small 32-bit test uses x86
emulation. Keep Docker Desktop's x86 emulation enabled. The runner stops on a build/test failure and reports
`complete host suite passed` only after both suites pass. Neither suite treats missing tools as SKIP.

The runner mounts only `milestone-7`, runs as your user, and writes results under `tests/build/native/` and
`tests/build/multilib/`. It sends just the Dockerfile to the builder, not the source tree or your local files.
The native container uses `--security-opt seccomp=unconfined` to allow TSAN's `setarch -R`; it is not privileged.

## Linux x86-64 without Docker

On Ubuntu 24.04:

```sh
sudo apt-get update
sudo apt-get install -y --no-install-recommends g++-multilib make python3 libssl-dev protobuf-compiler util-linux
cd milestone-7
make -k check
```

`make check` includes all native tests, protobuf schemas, TSAN and the 32-bit test. Missing `protoc` or a compiler
that can build/link `-m32` fails the run. Native ARM64 Linux developers should use the Docker recipe above for
the complete suite, since ARM64 GCC does not provide the x86 `-m32` target.

If TSAN reports `unexpected memory mapping` on an Ubuntu 24.04 test machine, temporarily use:

```sh
sudo sysctl -w vm.mmap_rnd_bits=28
```

This changes address randomization for the current boot of the test machine. CI applies it only on its
ephemeral runner. Containers use the host kernel, so this setting belongs on the Linux host, not inside Docker.

## Individual tests and logs

From `milestone-7`, with the corresponding compiler/tools installed:

```sh
make -C tests run-test_pcm       # one test
make -C tests tsan               # session lifecycle with ThreadSanitizer
make check-native               # everything except the separately run 32-bit test
make -C tests run-test_aa_proto32 # the remaining test (x86 with multilib)
```

`check-native` is one part of the complete suite, not a replacement for `check` or `tools/test-host.sh`.
Each test's linked sources are listed in [Makefile](Makefile). Test binaries and individual logs are in
`tests/build/` by default; the Docker runner uses the two subdirectories above. A failing test prints the log
tail in the console. `make -k` continues independent checks and still returns failure if any check fails.

The historical `rhmiapp_host_test.cpp`/`rhmiapp_fake_appframework.cpp` harness is not part of the Makefile suite;
its hand-written framework layout does not establish compatibility with the real HMI. The entry-key logic test
is included. QNX and HMI integration still require their own validation.

The [CI workflow](../../.github/workflows/host-tests.yml) runs on pushes, pull requests and manual dispatches
(manual dispatch becomes available after the workflow exists on the default branch).
