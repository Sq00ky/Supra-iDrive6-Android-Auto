"""Static source/data checks only; no compilation or C++ execution."""
import pathlib
import re
import subprocess
import unittest
ROOT=pathlib.Path(__file__).resolve().parents[1]
CLOCK=(ROOT/'src/cluster/hud6wb_calibration.hpp').read_text()
DISPLAY=(ROOT/'src/cluster/hud6wb_display.cpp').read_text()
RUNTIME=(ROOT/'src/cluster/nav_core.cpp').read_text()
KIT=(ROOT/'src/cluster/hud6wb_kit.cpp').read_text()

class PaletteCalibrationTests(unittest.TestCase):
    def test_compiled_indices_and_complete_semantic_mapping(self):
        for name,value in [('COLOR_CONTOUR',2),('COLOR_INFILL',14),('COLOR_ACTIVE_SELECTION',3)]:
            self.assertRegex(KIT,r'static const uint8_t '+name+r'\s*=\s*'+str(value)+r';')
        self.assertIn('p.graphite=COLOR_INFILL;p.gray=COLOR_INFILL;p.black=COLOR_INFILL',KIT)
        self.assertIn('p.gold=COLOR_CONTOUR;p.white=COLOR_ACTIVE_SELECTION',KIT)
        constructor=KIT.split('GrafiteKit::GrafiteKit()',1)[1].split('KitBarState kitBarState(',1)[0]
        self.assertNotIn('configuration()',constructor)
        self.assertNotIn('readProfile()',KIT)
        self.assertNotIn('palette(const Configuration&',KIT)

    def test_twenty_lines_in_known_visible_input_area(self):
        body=DISPLAY.split('Batch Hud6wbDisplay::calibrationBatch()',1)[1].split('bool Hud6wbDisplay::interceptNavigation(',1)[0]
        self.assertIn('p_.main.a!=0x1f||p_.main.b!=0x20',body)
        self.assertIn('index<20',body)
        self.assertIn('setLineColor(target,(uint8_t)index)',body)
        self.assertIn('kPolyline,points',body)
        ys=[75+(i*74+9)//19 for i in range(20)]
        self.assertEqual(len(set(ys)),20);self.assertEqual(ys[0],75);self.assertEqual(ys[-1],149)
        self.assertTrue(all(a<b for a,b in zip(ys,ys[1:])))
        self.assertIn('Point a={22,(int16_t)y},z={198,(int16_t)y}',body)
        self.assertLess(body.index('addPolygon('),body.index('kSwitchVisibility'))
        # clear + width + 20*(colour+line) + 8 hidden + swap + frame = 52
        self.assertLessEqual(2+20*2+8+2,128)

    def test_deadline_after_dispatch_and_no_timer_reset_on_nav_updates(self):
        self.assertIn('PALETTE_CALIBRATION_SECONDS = 10.0',CLOCK)
        self.assertIn('deadline_=now+PALETTE_CALIBRATION_SECONDS',CLOCK)
        self.assertIn('now>=deadline_',CLOCK)
        self.assertIn('status==1 && !route_',CLOCK)
        self.assertNotIn('status==3)',CLOCK)
        observe=CLOCK.split('void observe(',1)[1].split('Phase phase()',1)[0]
        self.assertNotIn('deadline_=now',observe)
        self.assertIn('if(!canDraw())return true',DISPLAY)
        self.assertIn('calibration_.dispatched(now)',DISPLAY)
        self.assertLess(DISPLAY.index('if(!canDraw())return true'),DISPLAY.index('calibration_.dispatched(now)'))

    def test_latest_snapshot_continues_after_interception(self):
        self.assertLess(RUNTIME.index('if(display_->interceptNavigation(pv,'),RUNTIME.index('core_->step(pv, now, why)'))
        body=DISPLAY.split('bool Hud6wbDisplay::interceptNavigation(',1)[1].split('bool Hud6wbDisplay::mainChanged(',1)[0]
        self.assertIn('Holding && !calibration_.due(now))return true',body)
        self.assertIn('calibration_.finished()',body)
        self.assertIn('return false;',body)
        self.assertNotIn('NavView v=',body)
        self.assertIn('nextUpdateIn(monoS())',RUNTIME)
        self.assertIn('us>50000',RUNTIME)
        self.assertNotIn('sleep(10)',DISPLAY)
        self.assertNotIn('usleep(10000000)',DISPLAY)

    def test_route_generation_retains_quick_stop_start_and_reroute(self):
        self.assertIn('status==1&&!self->route_active_',RUNTIME)
        self.assertIn('++self->navigation_epoch_',RUNTIME)
        self.assertIn('status==2)self->route_active_=false',RUNTIME)
        self.assertIn('navigationEpoch!=presented_navigation_epoch_',RUNTIME)
        self.assertIn('navigationRouteStarted()',RUNTIME)
        self.assertIn('navigationSessionEnded()',RUNTIME)
        self.assertIn('navigationPresentationCancelled()',RUNTIME)
        self.assertIn('ENABLE_PALETTE_CALIBRATION = true',CLOCK)
        self.assertIn('if(!enabled_)return',CLOCK)
        self.assertIn('deadline_-now:0.01',CLOCK) # no overdue-deadline busy loop

    def test_nav_decoder_and_transport_are_unchanged(self):
        paths=['src/cluster/nav_msg.hpp','src/aa_session.cpp','src/most','src/dsi_client.cpp','src/cluster/hud_terminal.cpp','src/cluster/hud_terminal.hpp']
        result=subprocess.run(['git','diff','62d1cf97fb6e2583429ed687ec43f491768fe871','--']+['milestone-7/'+p for p in paths],cwd=ROOT.parent,capture_output=True,check=True)
        self.assertEqual(result.stdout,b'')

if __name__=='__main__':unittest.main()
