"""Static integration contracts and Python fixtures only. Never invokes a compiler."""
import datetime
import pathlib
import re
import unittest
from work_renderer_integrity import assert_work_sources

ROOT = pathlib.Path(__file__).resolve().parents[1]
def read(name):
    return (ROOT / name).read_text(encoding='utf-8')

class AmigoStaticChecks(unittest.TestCase):
    def test_work_files_still_match_authoritative_package(self):
        assert_work_sources(self, ROOT)

    def test_no_active_size_discovery(self):
        active = read('src/cluster/hud6wb_resources.hpp').split('// ORIGINAL C04 DISCOVERY')[0]
        for forbidden in ('getSize(', 'imageSize(', 'waitIdle(', 'port.submit(', 'sizeReplyMs'):
            self.assertNotIn(forbidden, active)
        self.assertIn('port.status()==kReady', active)
        profile = read('src/cluster/hud6wb_profile.hpp')
        for measured in ('221,158', '305,44', '283,40', 'frameWidth(309)', 'frameHeight(330)', 'barWidth(28)', 'barHeight(113)'):
            self.assertIn(measured, profile)

    def test_session_boundaries_come_from_captured_status(self):
        source = read('src/nav_diag.hpp')
        self.assertIn('status==1&&!navigation_', source)
        self.assertIn('r.kind==2||status==0||status==2', source)
        self.assertNotIn('r.kind==2||status==3', source)
        self.assertIn('RawWire::status(r.body,r.size)', source)
        self.assertIn('navigation_session_start', source)
        self.assertIn('++navigationSerial_', source)
        self.assertIn('r.realSeconds', source)
        self.assertIn('CLOCK_REALTIME', source)
        self.assertIn('gmtime_r(&r.realSeconds', source)
        self.assertIn('unavailable_or_implausible', source)
        self.assertIn('system_clock_plausible_unverified', source)
        self.assertIn('refreshManualLabel();', source)
        self.assertIn('O_RDONLY|O_NONBLOCK', source)
        self.assertIn('sourceLabel(label)', source)
        # Independent fixture: repeated ACTIVE and rerouting are not new routes.
        active = False
        starts = []
        for index, status in enumerate((1, 1, 3, 1, 2, 1, 0, 1)):
            if status == 1 and not active:
                starts.append(index)
                active = True
            if status in (0, 2):
                active = False
        self.assertEqual(starts, [0, 5, 7])
        self.assertEqual(datetime.datetime.fromtimestamp(1794993332, datetime.timezone.utc).isoformat(),
                         '2026-11-18T09:15:32+00:00')

    def test_raw_and_legacy_exports_share_the_original_capture(self):
        source = read('src/nav_diag.hpp')
        self.assertIn('RawWire wire(r.body,r.size,config_.text)', source)
        self.assertIn('navigation_raw', source)
        self.assertIn('file.write(render(r))', source)
        self.assertIn('raw_body_hex', source)
        self.assertIn('redacted_ranges', source)
        self.assertIn('mask(i,size)', source)
        self.assertIn('known_message', source)
        self.assertIn('application_id\\\":null', source)
        session = read('src/aa_session.cpp')
        capture = session.index('nav_diag_->capture(msg_id')
        self.assertLess(capture, session.index('nav_cb_(msg_id,'))
        self.assertIn('frame_data.size()>2?frame_data.size()-2:0,channel)', session)

    def test_timing_is_bounded_and_off_rx_disk_path(self):
        trace = read('src/nav_diag_trace.hpp')
        self.assertIn('Capacity=128', trace)
        self.assertIn('pthread_mutex_trylock', trace)
        for forbidden in ('fwrite(', 'fflush(', 'usleep(', 'new ', 'std::string', 'fopen('):
            self.assertNotIn(forbidden, trace)
        core = read('src/cluster/nav_core.cpp')
        for stage in ('ParserEnter', 'StateStored', 'SnapshotBegin', 'SnapshotDecoded', 'DisplayBegin', 'DisplayEnd'):
            self.assertIn('navtrace::' + stage, core)
        terminal = read('src/cluster/hud_terminal.cpp')
        run = terminal.split('void HudTerminal::run() {', 1)[1]
        self.assertNotIn('cfg_', run)
        self.assertIn('core_.pacedSurfaceDiagnosticsEnabled()', run)
        self.assertIn('bool pacedSurfaceDiagnosticsEnabled() const { return cfg_.pacedGraphics && cfg_.surfaceDiagnostics; }',
                      read('src/cluster/hud_terminal.hpp'))
        self.assertLess(terminal.index('navtrace::MostBegin'), terminal.index('bool ok = fn && fn(m, user)'))
        self.assertGreater(terminal.index('navtrace::MostSend'), terminal.index('bool ok = fn && fn(m, user)'))
        self.assertIn('if (!ok) core_.sendFailed(now)', terminal)
        self.assertIn('else core_.sent(now)', terminal)

    def test_cpp_delimiters_and_local_include(self):
        pattern = r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\''
        for name in ('src/nav_diag.hpp', 'src/nav_diag_trace.hpp', 'src/cluster/nav_core.cpp',
                     'src/cluster/nav_core.hpp', 'src/cluster/hud6wb_resources.hpp', 'src/cluster/hud_terminal.cpp'):
            stack = []
            for c in re.sub(pattern, '', read(name), flags=re.S):
                if c in '([{':
                    stack.append(c)
                elif c in ')]}':
                    self.assertTrue(stack, name)
                    self.assertEqual(stack.pop(), {')': '(', ']': '[', '}': '{'}[c], name)
            self.assertFalse(stack, name)
        self.assertTrue((ROOT/'src/nav_diag_trace.hpp').is_file())

if __name__ == '__main__':
    unittest.main()
