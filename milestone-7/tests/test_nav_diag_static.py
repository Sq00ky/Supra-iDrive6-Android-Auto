"""Static contract checks only. Does not compile or execute any C++ code."""
import pathlib
import re
import subprocess
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
HEADER = (ROOT / 'src/nav_diag.hpp').read_text()
SESSION = (ROOT / 'src/aa_session.cpp').read_text()


class StaticNavigationDiagnostic(unittest.TestCase):
    def test_schema_matches_local_proto(self):
        proto = (ROOT / 'proto/aa_nav.proto').read_text()
        messages = dict(re.findall(r'message\s+(\w+)\s*\{([^}]*)\}', proto, re.S))
        mapping = {'Status': 'NavigationStatus', 'State': 'NavigationState', 'Step': 'Step',
                   'Maneuver': 'Maneuver', 'Road': 'Road', 'Lane': 'Lane',
                   'Direction': 'LaneDirection', 'Cue': 'Cue', 'Destination': 'Destination',
                   'Position': 'NavigationCurrentPosition', 'StepDistance': 'StepDistance',
                   'Distance': 'Distance', 'DestinationDistance': 'DestinationDistance',
                   'LegacyTurn': 'NextTurnEvent', 'LegacyDistance': 'NextTurnDistanceEvent'}
        block = HEADER.split('inline Field describe(', 1)[1].split('inline Node schema(', 1)[0]
        cases = dict(re.findall(r'case\s+(\w+):(.*?)(?=case\s+\w+:|default:)', block, re.S))
        for node, message in mapping.items():
            expected = re.findall(r'(optional|repeated)\s+(\w+)\s+(\w+)\s*=\s*(\d+)', messages[message])
            actual = re.findall(r'if\(f==(\d+)\)return Field\("([^"]+)",(\w+)(?:,(\w+)(?:,(true))?)?\)', cases[node])
            self.assertEqual(len(expected), len(actual), message)
            by_number = {number: (name, kind, child, repeated) for number, name, kind, child, repeated in actual}
            for repeated, field_type, name, number in expected:
                actual_name, kind, child, repeat_flag = by_number[number]
                self.assertEqual(actual_name, name, (message, number))
                self.assertEqual(bool(repeat_flag), repeated == 'repeated', (message, name))
                if field_type in messages:
                    self.assertEqual(kind, 'Nested')
                    self.assertEqual(mapping[child], field_type)
                elif field_type == 'string':
                    self.assertIn(kind, ('Text', 'Display', 'TimeText'))
                else:
                    self.assertEqual(kind, 'Boolean' if field_type == 'bool' else 'Integer')

    def test_hook_scope_and_order(self):
        capture = SESSION.index('nav_diag_->capture(msg_id')
        self.assertLess(SESSION.index('frame_data = decrypted_chunk;'), capture)
        self.assertLess(capture, SESSION.index('nav_cb_(msg_id,'))
        self.assertIn('if(channel==10 && nav_diag_)', SESSION[:capture][-120:])
        self.assertEqual(SESSION.count('nav_diag_->capture('), 1)
        self.assertIn('if (!nav_diag_ && navdiag::flag("NBT_NAV_DIAG"))', SESSION)

    def test_rx_has_no_blocking_or_io(self):
        body = HEADER.split('void enqueue(', 1)[1].split('std::string envelope(', 1)[0]
        self.assertIn('pthread_mutex_trylock', body)
        for forbidden in ('pthread_mutex_lock(', 'fwrite(', 'fflush(', 'printf(', 'new ',
                          'usleep(', 'std::string', 'Decoder ', 'rename('):
            self.assertNotIn(forbidden, body)
        self.assertIn('MaxBody=8192', HEADER)
        self.assertIn('Capacity=32', HEADER)
        self.assertIn('++stats_.lost', body)

    def test_safe_defaults_and_storage(self):
        self.assertIn('enabled(false),text(false)', HEADER)
        self.assertIn('value_omitted', HEADER)
        self.assertIn('redacted', HEADER)
        self.assertIn('O_APPEND', HEADER)
        self.assertIn('fchmod(fd,0600)', HEADER)
        self.assertIn('FD_CLOEXEC', HEADER)
        self.assertIn('path_+".1",two=path_+".2"', HEADER)
        self.assertIn('maxFileBytes(2u*1024u*1024u)', HEADER)
        self.assertIn('delete[] records_', HEADER)
        self.assertNotIn('free(records_)', HEADER)
        self.assertIn('delete nav_diag_;', SESSION)

    def test_balanced_cpp_delimiters(self):
        # Lexical check only, not a compiler or a claim of C++ type correctness.
        pattern = r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\''
        for relative in ('src/nav_diag.hpp', 'src/aa_session.cpp', 'src/aa_session.hpp', 'tests/test_nav_diag.cpp'):
            text = re.sub(pattern, '', (ROOT / relative).read_text(), flags=re.S)
            stack = []
            pairs = {')': '(', ']': '[', '}': '{'}
            for char in text:
                if char in '([{':
                    stack.append(char)
                elif char in pairs:
                    self.assertTrue(stack, relative)
                    self.assertEqual(stack.pop(), pairs[char], relative)
            self.assertFalse(stack, relative)

    def test_existing_6wb_and_most_unchanged(self):
        result = subprocess.run(['git', 'diff', 'HEAD', '--', 'milestone-7/src/cluster',
                                 'milestone-7/src/most', 'milestone-7/src/dsi_client.cpp',
                                 'milestone-7/src/sdr_builder.cpp', 'milestone-7/src/main.cpp'],
                                cwd=ROOT.parent, capture_output=True, check=True)
        self.assertEqual(result.stdout, b'')


if __name__ == '__main__':
    unittest.main()
