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
    def test_exit_roundabout_stays_standalone_in_both_modes_and_all_stages(self):
        reference = {}
        for mode in range(2):
            for stage in range(5):
                cells = render(stage, mode)
                exits = [next(c for c in cells if c.get('data-type') == str(t)) for t in (44, 46)]
                for cell in exits:
                    polys = cell.findall('.//s:polygon', NS)
                    self.assertEqual(len(polys), 3)  # departure arrow under context/route arcs; no exit number
                    self.assertEqual([int(p.get('data-native-colour')) for p in polys], [4, 3, 4])
                    self.assertNotIn('near', ''.join(cell.itertext()))
                    wire = [p.attrib for p in polys]
                    if stage == 0 and mode == 0:
                        reference[cell.get('data-type')] = wire
                    else:
                        self.assertEqual(wire, reference[cell.get('data-type')])
                for left, right in zip(exits[0].findall('.//s:polygon', NS), exits[1].findall('.//s:polygon', NS)):
                    a = [tuple(map(int, p.split(','))) for p in left.get('points').split()]
                    b = [tuple(map(int, p.split(','))) for p in right.get('points').split()]
                    self.assertEqual(a, [(124-x, y) for x, y in b])

    def test_measured_near_indices_and_stable_geometry(self):
        samples = {}
        for scheme, active in (("day", 7), ("night", 5)):
            cells = render(4, 0, scheme)
            turn = next(c for c in cells if c.get("data-type") == "6")
            polys = turn.findall(".//s:polygon", NS)
            indices = [int(p.get("data-native-colour")) for p in polys]
            self.assertEqual(set(indices), {3, active})
            self.assertEqual(indices.count(active), 2)  # body + head, no extra contour polygons
            samples[scheme] = [p.get("points") for p in polys]
        self.assertEqual(samples["day"], samples["night"])

    def test_far_and_ring_keep_their_working_palette(self):
        for scheme in ("day", "night"):
            cells = render(0, 0, scheme)
            far = next(c for c in cells if c.get("data-type") == "6")
            indices = [int(p.get("data-native-colour")) for p in far.findall(".//s:polygon", NS)]
            self.assertIn(4, indices)
            self.assertNotIn(5, indices)
            ring = next(c for c in cells if c.get("data-type") == "32" and c.get("data-angle") == "90"
                        and c.get("data-number") == "12")
            self.assertEqual({int(p.get("data-native-colour")) for p in ring.findall(".//s:polygon", NS)}, {3, 4})

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
                    for angle in (0, 1, 45, 90, 135, 180, 225, 270, 315, 359, 360) for number in range(9)}
        for direction, type_ in (("ccw", 34), ("cw", 32)):
            for mode in range(2):
                for stage in range(5):
                    cells = render(stage, mode, direction)
                    self.assertEqual(len(cells), 99)
                    self.assertEqual({int(c.get("data-type")) for c in cells}, {type_})
                    actual = {tuple(int(c.get(key)) for key in
                              ("data-has-angle", "data-angle", "data-has-number", "data-number")) for c in cells}
                    self.assertEqual(actual, expected)
                    for cell in cells:
                        angle = int(cell.get("data-angle"))
                        shown = 30 if angle == 1 else 330 if angle == 359 else angle
                        self.assertEqual(int(cell.get("data-rendered-angle")), shown)

    def test_invalid_cli_fails(self):
        for args in (("-1",), ("5",), ("x",), ("0", "2"), ("0", "0", "x"), ("0", "0", "cw", "extra")):
            self.assertEqual(subprocess.run([BINARY, *args], capture_output=True).returncode, 2)


if __name__ == "__main__":
    unittest.main()
