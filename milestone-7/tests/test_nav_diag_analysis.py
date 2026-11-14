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


if __name__ == '__main__':
    unittest.main()
