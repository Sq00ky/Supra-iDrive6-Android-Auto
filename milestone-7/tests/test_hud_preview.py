#!/usr/bin/env python3
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
"""The visual review must contain the phone's independent angle/exit-number combinations."""
from pathlib import Path
import subprocess
import sys
import unittest
import xml.etree.ElementTree as ET

BINARY = str(Path(sys.argv.pop(1)).resolve())
NS = {"s": "http://www.w3.org/2000/svg"}


def render(*args):
    result = subprocess.run([BINARY, *map(str, args)], check=True, capture_output=True, text=True)
    root = ET.fromstring(result.stdout)
    cells = root.findall(".//s:g[@data-type]", NS)
    for cell in cells:
        assert cell.findall(".//s:polygon", NS), "a catalog cell must contain a production drawing"
        picture = cell.find("s:svg", NS)
        assert picture.get("viewBox") == "0 0 126 77"
    return cells


class PreviewTest(unittest.TestCase):
    def test_every_type_and_known_angle_two_digit_numbers(self):
        for mode in range(2):
            for stage in range(5):
                cells = render(stage, mode)
                self.assertEqual(len(cells), 89)
                self.assertEqual({int(c.get("data-type")) for c in cells}, set(range(61)))
                examples = {(int(c.get("data-type")), int(c.get("data-has-angle")),
                             int(c.get("data-angle")), int(c.get("data-number"))) for c in cells}
                for type_ in (32, 34):
                    self.assertIn((type_, 1, 90, 12), examples)
                    self.assertIn((type_, 1, 180, 99), examples)

    def test_ring_angle_and_number_are_independent(self):
        expected = {(int(angle != 0), angle, int(number != 0), number)
                    for angle in (0, 45, 90, 135, 180, 225, 270, 315, 360) for number in range(9)}
        for direction, type_ in (("ccw", 34), ("cw", 32)):
            for mode in range(2):
                for stage in range(5):
                    cells = render(stage, mode, direction)
                    self.assertEqual(len(cells), 81)
                    self.assertEqual({int(c.get("data-type")) for c in cells}, {type_})
                    actual = {tuple(int(c.get(key)) for key in
                              ("data-has-angle", "data-angle", "data-has-number", "data-number")) for c in cells}
                    self.assertEqual(actual, expected)

    def test_invalid_cli_fails(self):
        for args in (("-1",), ("5",), ("x",), ("0", "2"), ("0", "0", "x"), ("0", "0", "cw", "extra")):
            self.assertEqual(subprocess.run([BINARY, *args], capture_output=True).returncode, 2)


if __name__ == "__main__":
    unittest.main()
