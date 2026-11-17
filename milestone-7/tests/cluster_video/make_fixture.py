#!/usr/bin/env python3
"""Synthetic H.264 access units, never a recording of phone/car data."""

from pathlib import Path
import os
import re
import shutil
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
FRAME_COUNT = 30


def packet(kind, payload=b"", timestamp=0):
    # ACP1 header: magic, kind, stream epoch, payload length, presentation time.
    return struct.pack("!IIIIQ", 0x41435031, kind, 3, len(payload), timestamp) + payload


def write_fixture(destination, ffmpeg, name, source):
    h264_path = destination / (name + ".h264")
    subprocess.run(
        [
            ffmpeg,
            "-hide_banner",
            "-loglevel",
            "error",
            "-y",
            "-f",
            "lavfi",
            "-i",
            source,
            "-frames:v",
            str(FRAME_COUNT),
            "-an",
            "-c:v",
            "libx264",
            "-profile:v",
            "baseline",
            "-pix_fmt",
            "yuv420p",
            "-x264-params",
            "aud=1:repeat-headers=1:keyint=10:bframes=0:threads=1",
            "-f",
            "h264",
            str(h264_path),
        ],
        check=True,
    )
    encoded = h264_path.read_bytes()
    access_unit_starts = []
    for match in re.finditer(b"\x00\x00(?:\x00)?\x01", encoded):
        if encoded[match.end()] & 31 == 9:  # H.264 access unit delimiter NAL.
            access_unit_starts.append(match.start())
    assert access_unit_starts[0] == 0 and len(access_unit_starts) == FRAME_COUNT
    access_unit_starts.append(len(encoded))

    wire = packet(1)  # Reset before the first access unit.
    for frame in range(FRAME_COUNT):
        access_unit = encoded[access_unit_starts[frame] : access_unit_starts[frame + 1]]
        wire += packet(2, access_unit, frame * 100000)
    wire += packet(3)  # Stop after the final access unit.
    (destination / (name + ".acp")).write_bytes(wire)
    print(f"{name}: {FRAME_COUNT} access units, {len(wire)} bytes")


def main():
    destination = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "tests/build/cluster_video"
    destination.mkdir(parents=True, exist_ok=True)
    ffmpeg = os.environ.get("FFMPEG") or shutil.which("ffmpeg")
    if not ffmpeg:
        raise RuntimeError("Set FFMPEG or install ffmpeg on PATH")
    write_fixture(destination, ffmpeg, "fixture", "testsrc2=size=800x480:rate=10")


if __name__ == "__main__":
    main()
