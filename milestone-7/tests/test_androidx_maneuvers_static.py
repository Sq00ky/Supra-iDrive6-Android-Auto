"""Capture fixtures and source regression proof. No C++ compilation/execution."""
import hashlib
import json
import pathlib
import re
import subprocess
import unittest
from work_renderer_integrity import assert_work_sources

ROOT = pathlib.Path(__file__).resolve().parents[1]
BASE = '473b393e2ad6894d54b8b284bfa07c8cdc7ec52b'
def read(path):
    return (ROOT/path).read_text(encoding='utf-8')
def old(path):
    return subprocess.check_output(['git', 'show', BASE+':milestone-7/'+path], cwd=ROOT.parent).decode().replace('\r', '')
def wire_fields(data):
    i = 0
    def integer():
        nonlocal i
        n = shift = 0
        while i < len(data):
            c = data[i]
            i += 1
            n |= (c & 127) << shift
            if c < 128:
                return n
            shift += 7
        raise ValueError('short fixture varint')
    while i < len(data):
        tag = integer()
        if tag & 7 == 0:
            value = integer()
        elif tag & 7 == 2:
            size = integer()
            value = data[i:i+size]
            if len(value) != size:
                raise ValueError('short fixture message')
            i += size
        else:
            raise ValueError('unexpected fixture wire type')
        yield tag >> 3, value

class AndroidxManeuverChecks(unittest.TestCase):
    def test_all_documented_types_select_the_intended_work_artwork(self):
        spec = json.loads(read('tests/fixtures/androidx_maneuver_types.json'))
        mapping = {int(n): name for n, name in re.findall(r'case (\d+):return sixwbkit::Maneuver::(\w+);', read('src/cluster/hud6wb_kit.cpp'))}
        self.assertEqual(len(spec['types']), 49)
        for row in spec['types']:
            self.assertEqual(mapping[row['value']], row['work_role'], row['androidx'])
        for number, role in ((32,'RoundaboutCW'),(33,'RoundaboutCWAngle'),(34,'RoundaboutCCW'),
                             (35,'RoundaboutCCWAngle'),(43,'RoundaboutEnterCW'),(44,'RoundaboutExitCW'),
                             (45,'RoundaboutEnterCCW'),(46,'RoundaboutExitCCW'),(39,'Destination'),
                             (47,'FerryLeft'),(48,'FerryRight'),(49,'TrainFerryLeft'),(50,'TrainFerryRight')):
            self.assertEqual(mapping[number], role)

    def test_parser_changes_are_only_the_new_lookahead_range(self):
        current = read('src/cluster/nav_msg.hpp')
        self.assertEqual(current.count('nt>0 && nt<=50'), 1)
        self.assertEqual(current.replace('nt>0 && nt<=50', 'nt>0 && nt<=46'), old('src/cluster/nav_msg.hpp'))
        self.assertNotIn('NBT_NAV_DIAG_SOURCE', current)
        self.assertNotIn('getenv(', current)

    def regression_for(self, label):
        fixture = json.loads(read('tests/fixtures/nav_androidx_captures.json'))
        capture = next(c for c in fixture['captures'] if c['label_for_test_only'] == label)
        self.assertTrue(all(c['type'] <= 46 for c in capture['cases']))
        self.assertEqual(sum(c['occurrences'] for c in capture['cases']), capture['state_messages'])
        # For this entire old input domain, parser and adapter statements are unchanged.
        self.test_parser_changes_are_only_the_new_lookahead_range()
        current = dict(re.findall(r'case (\d+):return sixwbkit::Maneuver::(\w+);', read('src/cluster/hud6wb_kit.cpp')))
        previous = dict(re.findall(r'case (\d+):return sixwbkit::Maneuver::(\w+);', old('src/cluster/hud6wb_kit.cpp')))
        for case in capture['cases']:
            self.assertEqual(current[str(case['type'])], previous[str(case['type'])])
        assert_work_sources(self, ROOT)  # Includes unchanged Work display/renderer and exact adapter-only overlay.

    def test_google_maps_real_capture_regression(self):
        self.regression_for('maps')

    def test_waze_real_capture_regression(self):
        self.regression_for('waze')

    def test_real_maneuver_submessages_and_provenance(self):
        fixture = json.loads(read('tests/fixtures/nav_androidx_captures.json'))
        for capture in fixture['captures']:
            source = ROOT.parent/capture['source']
            if source.exists():
                self.assertEqual(hashlib.sha256(source.read_bytes()).hexdigest(), capture['sha256'])
            for case in capture['cases']:
                state = dict(wire_fields(bytes.fromhex(case['state_hex'])))
                maneuver = dict(wire_fields(dict(wire_fields(state[1]))[1]))
                self.assertEqual(maneuver[1], case['type'])
                self.assertEqual(maneuver.get(2), case['exit_number'])
                self.assertEqual(maneuver.get(3), case['exit_angle'])

    def test_amigo_missing_exit_is_not_fabricated_or_inverted(self):
        fixture = json.loads(read('tests/fixtures/nav_androidx_captures.json'))
        amigo = next(c for c in fixture['captures'] if c['label_for_test_only'] == 'amigo')
        entries = [c for c in amigo['cases'] if c['type'] in (43,45)]
        self.assertEqual(sum(c['occurrences'] for c in entries), 25)
        self.assertTrue(all(c['exit_number'] is None and c['exit_angle'] is None for c in entries))
        self.assertFalse(any(c['type'] in (32,33,34,35,44,46) for c in amigo['cases']))
        self.assertTrue(all(c['wire_origin']=='captured_maneuver_submessage' for c in entries))
        # Waze uses the same data pattern, so a blanket ENTER rewrite would violate compatibility.
        waze = next(c for c in fixture['captures'] if c['label_for_test_only'] == 'waze')
        self.assertTrue(any(c['type']==45 and c['exit_number'] is None and c['exit_angle'] is None for c in waze['cases']))

    def test_new_ferries_use_existing_art_and_no_new_normalization_state(self):
        adapter = read('src/cluster/hud6wb_kit.cpp')
        self.assertIn('if(v.aa_type<0||v.aa_type>50)return false', adapter)
        self.assertIn('renderManeuver(e,area,0.0)', adapter)
        self.assertNotIn('NBT_NAV_DIAG_SOURCE', adapter)
        assert_work_sources(self, ROOT)

if __name__ == '__main__':
    unittest.main()
