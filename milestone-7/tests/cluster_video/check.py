#!/usr/bin/env python3
"""Host regressions for the G30 stream, native player lifecycle and pixel contract."""

import concurrent.futures
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
SOURCE = ROOT / "src/cluster/video"
VENDOR = ROOT / "third_party/openh264"
OUTPUT = ROOT / "tests/build/cluster_video"

COMPILER = shlex.split(os.environ.get("CXX", "clang++"))
COMPILER_FLAGS = [
    "-std=c++11",
    "-DNBT_CLUSTER_PROFILE_G30",
    "-O1",
    "-g",
    "-fsanitize=address,undefined",
    "-fno-omit-frame-pointer",
]
INCLUDES = ["-I" + str(ROOT / "src")] + [
    "-I" + str(VENDOR / "codec" / directory)
    for directory in ("api", "api/wels", "common/inc", "decoder/core/inc", "decoder/plus/inc")
]
TEST_ENV = dict(
    os.environ,
    UBSAN_OPTIONS="halt_on_error=1",
    ASAN_OPTIONS="detect_leaks=0" if sys.platform == "darwin" else "detect_leaks=1",
)
TEST_PROGRAMS = (
    "recovery",
    "player_service",
    "router",
    "manifest",
    "router_faults",
    "encoded_queue",
    "live_setting",
    "screen",
    "control",
    "control_recovery",
    "pixels",
    "player_lifecycle",
    "service_log",
)
NATIVE_PEER_TESTS = {"screen", "control", "control_recovery", "pixels", "player_lifecycle"}
DECODER_TESTS = {"pixels", "player_lifecycle"}


def run(name, command, cwd=ROOT):
    """Keep a full stage log and include its tail in any failure."""
    process = subprocess.run(
        [str(argument) for argument in command],
        cwd=cwd,
        env=TEST_ENV,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        timeout=180,
    )
    (OUTPUT / (name + ".log")).write_text(process.stdout)
    if process.returncode:
        raise RuntimeError(name + " failed:\n" + process.stdout[-4000:])
    return process.stdout


def compile_decoder_source(source):
    object_file = OUTPUT / (source.stem + ".o")
    run(
        "compile-" + source.stem,
        [
            *COMPILER,
            *COMPILER_FLAGS,
            "-Wno-deprecated-register",
            "-fno-exceptions",
            "-fno-rtti",
            "-fno-strict-aliasing",
            *INCLUDES,
            "-c",
            source,
            "-o",
            object_file,
        ],
    )
    return object_file


def build_decoder_archive():
    sources = [
        source
        for source in sorted((VENDOR / "codec/common/src").glob("*.cpp"))
        if not source.name.startswith("Wels")
    ]
    sources += [
        source
        for source in sorted((VENDOR / "codec/decoder/core/src").glob("*.cpp"))
        if source.name != "wels_decoder_thread.cpp"
    ]
    sources += [VENDOR / "codec/decoder/plus/src/welsDecoderExt.cpp", SOURCE / "decoder_port.cpp"]
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        objects = list(pool.map(compile_decoder_source, sources))
    archive = OUTPUT / "decoder.a"
    archive.unlink(missing_ok=True)
    run("archive", ["ar", "rcs", archive, *objects])
    return archive


def openssl_flags():
    flags = shlex.split(os.environ.get("OPENSSL_CFLAGS", ""))
    if flags:
        return flags
    process = subprocess.run(
        ["pkg-config", "--cflags", "openssl"],
        stdout=subprocess.PIPE,
        text=True,
    )
    if process.returncode:
        raise RuntimeError("Set OPENSSL_CFLAGS to your OpenSSL include directory")
    return shlex.split(process.stdout)


def main():
    OUTPUT.mkdir(parents=True, exist_ok=True)
    # A stale result must never make a failed/incomplete run look successful.
    (OUTPUT / "result.json").unlink(missing_ok=True)
    passed = []
    for profile in ("j29", "g30"):
        flags = [] if profile == "g30" else ["-UNBT_CLUSTER_PROFILE_G30"]
        run("compile-profile-" + profile, [*COMPILER, *COMPILER_FLAGS, *flags,
            *INCLUDES, HERE / "test_profiles.cpp", "-o", OUTPUT / ("profile-" + profile)])
        run("profile-" + profile, [OUTPUT / ("profile-" + profile)])
        passed.append("profile-" + profile)
        print("PASS profile-" + profile, flush=True)
    archive = build_decoder_archive()
    run("fixture", [sys.executable, HERE / "make_fixture.py", OUTPUT])

    for name in TEST_PROGRAMS:
        peer_flags = ["-DNBT_CLUSTER_HOST_TEST", "-I" + str(HERE)] if name in NATIVE_PEER_TESTS else []
        libraries = [archive] if name in DECODER_TESTS else []
        run(
            "compile-" + name,
            [
                *COMPILER,
                *COMPILER_FLAGS,
                *peer_flags,
                "-pthread",
                *INCLUDES,
                HERE / f"test_{name}.cpp",
                *libraries,
                "-o",
                OUTPUT / name,
            ],
        )
        run(name, [OUTPUT / name], OUTPUT)
        passed.append(name)
        print("PASS", name, flush=True)

    # Exercise AASession itself with test TLS rather than a router-only peer.
    session_sources = [
        ROOT / path
        for path in (
            "src/aa_session.cpp",
            "src/audio/audio_acks.cpp",
            "src/crash_handler.cpp",
            "src/sdr_builder.cpp",
        )
    ]
    run(
        "compile-source-session",
        [
            *COMPILER,
            *COMPILER_FLAGS,
            "-pthread",
            "-DNBT_HOST_TEST",
            "-DNBT_CLUSTER_VIDEO",
            "-include",
            ROOT / "tests/stub_cryptor.hpp",
            "-I" + str(ROOT / "src"),
            "-I" + str(ROOT / "include"),
            *openssl_flags(),
            HERE / "test_source_session.cpp",
            *session_sources,
            "-o",
            OUTPUT / "source-session",
        ],
    )
    run("source-session", [OUTPUT / "source-session"])
    passed.append("source-session")
    print("PASS source-session", flush=True)

    run(
        "compile-supervisor",
        [
            *COMPILER,
            *COMPILER_FLAGS,
            *INCLUDES,
            HERE / "supervisor_fixture.cpp",
            SOURCE / "supervisor.cpp",
            "-o",
            OUTPUT / "supervisor-fixture",
        ],
    )
    TEST_ENV["CLUSTER_TEST_SUPERVISOR_BIN"] = str(OUTPUT / "supervisor-fixture")
    run("supervisor", [sys.executable, HERE / "test_supervisor.py"])
    passed.append("native-supervisor (15 tests)")
    print("PASS native-supervisor (15 tests)", flush=True)
    (OUTPUT / "result.json").write_text(
        json.dumps(
            {
                "passed": passed,
                "native_qnx_build": False,
                "physical_car_test": False,
            },
            indent=2,
        )
        + "\n"
    )


if __name__ == "__main__":
    main()
