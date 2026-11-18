"""Python consumer tests with synthetic JSONL only; no C++ or vehicle execution."""
import importlib.util
import json
import pathlib
import tempfile
import sys
sys.dont_write_bytecode = True
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('nav_analysis', ROOT / 'tools/analyze_nav_diag.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class AnalysisTests(unittest.TestCase):
    def test_rotation_order_duplicates_and_privacy(self):
        with tempfile.TemporaryDirectory() as folder:
            paths = []
            for index in (2, 1, 0):
                row = {'version': 1, 'record': 'navigation', 'pid': 12, 'run_mono_ns': 999,
                       'session': 1, 'sequence': index, 'mono_ns': index * 1000,
                       'source': 'maps', 'message_id': 0x8006, 'seen': index + 1,
                       'captured': index + 1, 'lost': 0, 'truncated': 0,
                       'fields': [{'path': 'destinations[0].address', 'field': 1, 'wire': 2,
                                   'size': 12, 'known': True, 'value': 'PRIVATE PLACE'},
                                  {'path': 'unknown_99', 'field': 99, 'wire': 0, 'size': 1, 'known': False}]}
                path = pathlib.Path(folder) / ('nav.' + str(index))
                path.write_bytes((json.dumps(row) + '\n').encode('utf-8'))
                paths.append(path)
            result = module.summarize(paths + [paths[0]])
            self.assertEqual(result['duplicate_records'], 1)
            self.assertEqual(result['sessions'][0]['retained_messages'], 3)
            self.assertEqual(result['sessions'][0]['messages']['0x8006'], 3)
            self.assertNotIn('PRIVATE PLACE', json.dumps(result))

    def test_malformed_tail_is_not_rewritten(self):
        with tempfile.TemporaryDirectory() as folder:
            path = pathlib.Path(folder) / 'nav.jsonl'
            content = b'{"partial":'
            path.write_bytes(content)
            self.assertEqual(module.summarize([path])['invalid_lines'], 1)
            self.assertEqual(path.read_bytes(), content)

    def test_multiple_navigation_sessions_and_timing_in_one_connection(self):
        with tempfile.TemporaryDirectory() as folder:
            base = {'version': 1, 'pid': 12, 'run_mono_ns': 100, 'session': 1,
                    'source_kind': 'manual_label', 'application_id': None}
            rows = []
            for number, label in enumerate(('maps', 'amigo'), 1):
                uid = 'route-' + str(number)
                rows.append(dict(base, record='navigation_session_start', sequence=number*2,
                                 mono_ns=number*1000000000, source=label, navigation_session_id=uid,
                                 wall_time='2026-11-18T09:15:32Z', wall_time_valid=True))
                rows.append(dict(base, record='navigation', sequence=number*2+1,
                                 mono_ns=number*1000000000+5000000, source=label, navigation_session_id=uid,
                                 message_id=0x8006, fields=[{'path': 'steps[0].maneuver.type', 'value': 34},
                                                          {'path': 'steps[0].maneuver.exit_number', 'value': 2},
                                                          {'path': 'steps[0].maneuver.exit_angle', 'value': 180}]))
            rows.append(dict(base, record='navigation_timing', sequence=1, mono_ns=1010000000,
                             rx_mono_ns=1000000000, rx_sequence=2, stage='snapshot_decoded'))
            path = pathlib.Path(folder)/'nav.jsonl'
            path.write_text(''.join(json.dumps(row)+'\n' for row in rows), encoding='utf-8')
            result = module.summarize([path])
            self.assertEqual(result['invalid_lines'], 0)
            self.assertEqual(len(result['navigation_sessions']), 2)
            self.assertEqual([r['source_label'] for r in result['navigation_sessions']], ['maps', 'amigo'])
            self.assertTrue(all(r['application_id'] is None for r in result['navigation_sessions']))
            self.assertEqual(result['navigation_sessions'][1]['roundabout_updates'][0]['exit_angle'], 180)
            self.assertEqual(result['timing']['snapshot_decoded']['max_since_rx_ms'], 10)


if __name__ == '__main__':
    unittest.main()
