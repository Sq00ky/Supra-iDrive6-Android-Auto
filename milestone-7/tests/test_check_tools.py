#!/usr/bin/env python3
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
"""A missing test tool must not turn the complete suite green."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

MAKE = shutil.which("make")
HERE = Path(__file__).resolve().parent


class RequiredTools(unittest.TestCase):
    def test_missing_protoc_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            env = dict(os.environ, PATH=tmp, MAKEFLAGS="")
            result = subprocess.run(
                [MAKE, "-o", tmp + "/dump_messages", "run-test_schema", "OUT=" + tmp],
                cwd=HERE, env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            self.assertNotEqual(result.returncode, 0, result.stdout)
            self.assertIn("[FAIL] test_schema.py: protoc is required", result.stdout)
            self.assertNotIn("[SKIP]", result.stdout)

    def test_missing_multilib_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            result = subprocess.run(
                [MAKE, "run-test_aa_proto32", "HOST_CXX=false", "OUT=" + tmp],
                cwd=HERE, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            self.assertNotEqual(result.returncode, 0, result.stdout)
            self.assertIn("[FAIL] test_aa_proto (32-bit): a working -m32 toolchain is required", result.stdout)
            self.assertNotIn("[SKIP]", result.stdout)


if __name__ == "__main__":
    unittest.main()
