"""Source contracts and numerical regressions. No C++ compilation or execution."""
import math
import pathlib
import re
import subprocess
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
CLUSTER = ROOT / 'src/cluster'


def read(name):
    return (CLUSTER / name).read_text()


def section(source, start, end):
    return source.split(start, 1)[1].split(end, 1)[0]


class RecoveryChecks(unittest.TestCase):
    def test_startup_experiments_and_lifecycle_are_removed(self):
        for name in ('hud6wb_display.cpp', 'hud6wb_display.hpp', 'display.hpp',
                     'nav_core.cpp', 'nav_core.hpp', 'hud6wb_codec.cpp'):
            source = read(name)
            for obsolete in ('calibration_', 'PaletteCalibration', 'calibrationPicture',
                             'interceptNavigation', 'nextUpdateIn', 'straight_',
                             'selectCentralSymbol', 'navigation_epoch_'):
                self.assertNotIn(obsolete, source)
        self.assertFalse((CLUSTER / 'hud6wb_calibration.hpp').exists())

    def test_bar_threshold_uses_phone_metres_and_preserves_central_symbol(self):
        source = read('hud6wb_kit.cpp')
        expression = re.search(r's\.visible=([^;]+);', source).group(1)
        expression = expression.replace('kombinav::distanceMetres(v,metres)', 'known')
        expression = expression.replace('sixwbkit::finite(metres)', 'math.isfinite(metres)')
        expression = expression.replace('&&', ' and ')
        for metres, known, expected in ((501, True, False), (500, True, True),
                                        (499, True, True), (0, True, True),
                                        (-1, True, False), (math.inf, True, False),
                                        (math.nan, True, False), (50, False, False)):
            self.assertEqual(bool(eval(expression, {'math': math}, locals())), expected)
        display = read('hud6wb_display.cpp')
        self.assertIn('kit_.central(v,target,useful,drawing)', display)
        self.assertNotIn('aa_type=36', display)
        self.assertIn('bar.visible!=oldBar.visible', display)
        self.assertIn('setVisibility(p_.bar,bar.visible)', display)
        self.assertIn('barRangeMetres(500)', read('hud6wb_profile.hpp'))

    def test_no_graphics_ack_barrier_and_no_missing_reply_failure(self):
        source = read('hud_terminal.cpp')
        functions = section(source, 'bool Core::graphicFunction', 'bool Core::asynchronous')
        for method in ('kAddPolygon', 'kSetFillColor', 'kSwitchVisibility',
                       'kSetVisibility', 'kRemoveAllItems', 'kTextareaSetText', '0x227', '0xC05'):
            self.assertIn(method, functions)
        self.assertNotIn('kGetSize', functions)
        self.assertIn('if (!asynchronous(c)) inFlight_.push_back(f)', source)
        finish = section(source, 'void Core::finishIfDone', 'void Core::onMessage')
        self.assertNotIn('graphics_.', finish)
        self.assertIn('current_ = pending_.front()', finish)
        self.assertIn('pending_.erase(pending_.begin())', finish)
        self.assertIn('else core_.sent(now)', source)
        expire = section(source, 'void Core::expireGraphics', 'bool Core::rememberGraphic')
        self.assertNotIn('fail(', expire)

    def test_c04_errors_timeouts_and_duplicates_release_the_queue(self):
        source = read('hud_terminal.cpp')
        for outcome, following in (('invalid', 'finishSizeQuery(i,now)'),
                                   ('error', 'finishSizeQuery(i,now)'),
                                   ('timeout', 'finishSizeQuery(0,now)'),
                                   ('send-error', 'finishSizeQuery(inFlight_.size()-1,now)')):
            branch = source.split('surfaceRecord("' + outcome + '"', 1)[1]
            self.assertIn(following, branch[:650])
            self.assertIn('cfg_.surfaceDiagnostics && cfg_.pacedGraphics', branch[:650])
        duplicate = source.split('surfaceRecord("duplicate-suppressed"', 1)[1][:250]
        self.assertIn('++nextCall_', duplicate)
        self.assertIn('finishIfDone(now)', duplicate)
        factory = section(read('nav_core.cpp'), 'if (!strcmp(id, "6wb"))', 'return false;\n}')
        self.assertIn('c.surfaceDiagnostics=!shadow_', factory)
        self.assertIn('c.sizeReplyMs=5000', factory)
        self.assertIn('c.graphicIntervalMs=15', factory)
        before = read('nav_core.cpp').split('if (!strcmp(id, "6wb"))')[0]
        self.assertNotIn('c.surfaceDiagnostics=', before)

    def test_surface_log_contains_raw_observations_and_bounded_persistent_storage(self):
        source = read('hud_terminal.cpp')
        record = section(source, 'void Core::surfaceRecord', 'void Core::finishSizeQuery')
        for field in ('terminal=82/%02X', 'handle=%04X', 'outcome=%s', 'elapsed_ms=%u',
                      'width=%u', 'height=%u', 'payload=', 'gmtime_r', 'mono_ms=%u'):
            self.assertIn(field, record)
        self.assertIn('rdBe16(response->data+2)', record)
        self.assertNotIn('221', record)
        self.assertNotIn('158', record)
        self.assertIn('surfaceRecords_.size()>=64', record)
        self.assertIn('logs/6wb-surfaces.log', source)
        self.assertIn('768u*1024u', source)
        self.assertIn('nbt::packaged()', source)
        writer = source.split('core_.takeSurfaceRecords(records)', 1)[1]
        self.assertLess(writer.index('pthread_mutex_unlock(&lock_)'),
                        writer.index('surfaces.write(records[i])'))
        self.assertIn('navigation continues', writer)

    def test_optional_surface_failure_does_not_block_central_reference(self):
        resources = read('hud6wb_resources.hpp')
        match = section(resources, 'bool matches(', 'static void add(')
        for handle in ('p_.base.frame', 'p_.main.a', 'p_.main.b'):
            self.assertIn('compatibleOrMissing(port,' + handle, match)
        for optional in ('p_.icons', 'p_.lanes', 'p_.bar'):
            self.assertNotIn(optional, match)
        self.assertIn('if(!port.waitIdle(0))return false', resources)
        self.assertIn('!port.imageSize(handle,rw,rh)', resources)
        self.assertIn('iconBitmaps(false)', read('hud6wb_profile.hpp'))
        self.assertIn('NBT_6WB_ICON_BITMAPS', read('hud6wb_config.hpp'))

    def test_drawn_buffer_is_shown_after_clear_and_prediction_follows_submission(self):
        source = read('hud6wb_display.cpp')
        render = section(source, 'Batch Hud6wbDisplay::render', 'void Hud6wbDisplay::remember')
        clear = render.index('removeAllItems(target)')
        draw = render.index('drawing.calls.begin()', clear)
        swap = render.index('switchVisibility(mainVisible_,target)', draw)
        show = render.index('setVisibility(target,true)', swap)
        self.assertLess(clear, draw)
        self.assertLess(draw, swap)
        self.assertLess(swap, show)
        send = section(source, 'ClusterDisplay::Result Hud6wbDisplay::showManeuver',
                       'ClusterDisplay::Result Hud6wbDisplay::updateDistance')
        self.assertLess(send.index('if(!submit(chunk))'),
                        send.index('remember(queuedView_,queuedPicture_)'))
        self.assertIn('queuedCall_==queuedPicture_.calls.size()', send)
        self.assertIn('b=Batch("6wb-unrepresentable"', send)
        self.assertIn('return kOk', send)

    def test_thin_collinear_quantized_diagonal_is_empty_instead_of_fatal(self):
        points = [(10, 10), (20, 20), (30, 30)]
        area = sum(a[0]*b[1]-a[1]*b[0]
                   for a, b in zip(points, points[1:]+points[:1]))
        self.assertEqual(area, 0)
        self.assertGreater(max(x for x, y in points)-min(x for x, y in points), 1)
        dx, dy = points[1][0]-points[0][0], points[1][1]-points[0][1]
        self.assertTrue(all(dx*(y-points[0][1])-dy*(x-points[0][0]) == 0
                            for x, y in points[2:]))
        source = read('hud6wb_kit.cpp')
        self.assertIn('dx*(q[i].y-q[0].y)-dy*(q[i].x-q[0].x)', source)

    def test_tx_default_and_explicit_choices(self):
        profile = read('hud6wb_profile.hpp')
        config = read('hud6wb_config.hpp')
        self.assertIn('vectorTx(4)', profile)
        self.assertIn('if(tx==0 || tx==4)', config)
        self.assertIn('NBT_6WB_TX', config)
        self.assertIn('c.txType=p.vectorTx', read('nav_core.cpp'))
        self.assertIn('c.txType=0', read('hud6wb_display.cpp'))

    def test_artwork_and_extended_street_text_are_preserved(self):
        self.assertEqual(subprocess.check_output(
            ['git', 'diff', 'HEAD', '--', 'milestone-7/src/cluster/grafite_oro'],
            cwd=ROOT.parent), b'')
        self.assertIn('base.streetChars=kMaxLatinTextBytes', read('hud6wb_profile.hpp'))
        self.assertIn('if (!d_.useFullRoadName())', read('nav_core.cpp'))
        display = read('hud6wb_display.cpp')
        self.assertIn('latinText(p_.base.street,v.road,p_.base.streetChars)', display)
        self.assertIn('street!=latinText(p_.base.street,shown_.road', display)


if __name__ == '__main__':
    unittest.main()
