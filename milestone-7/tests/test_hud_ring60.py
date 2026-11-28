#!/usr/bin/env python3
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
"""Compare the C++ port with the accepted artwork, frozen before the port was written."""
import json
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
binary = str(Path(sys.argv[1]).resolve())
expected = json.loads((root / "tests/fixtures/hud-ring60-approved.json").read_text())["hashes"]
result = subprocess.run([binary, "--hashes"], check=True, capture_output=True, text=True)
actual = [[int(n) for n in line.split()] for line in result.stdout.splitlines()]
assert len(actual) == len(expected) == 722
for i, (got, want) in enumerate(zip(actual, expected)):
    assert got == want, "accepted drawing differs at angle%d direction%d: %r != %r" % (i//2, i%2, got, want)
print("7220 accepted ring/number/direction combinations match the C++ port")
