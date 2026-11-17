"""Expanded vector-coordinate contracts; source/data only, never a compiler."""
import pathlib
import subprocess
import unittest
ROOT=pathlib.Path(__file__).resolve().parents[1]
BASE='c55ce6a2f7322f5d718bc88f8c863eb90680780b'
PROFILE=(ROOT/'src/cluster/hud6wb_profile.hpp').read_text()
RESOURCES=(ROOT/'src/cluster/hud6wb_resources.hpp').read_text()
KIT=(ROOT/'src/cluster/hud6wb_kit.cpp').read_text()
CONFIG=(ROOT/'src/cluster/hud6wb_config.hpp').read_text()

def old(name):return subprocess.check_output(['git','show',BASE+':milestone-7/src/cluster/'+name],cwd=ROOT.parent,text=True)

class ExpandedGeometryChecks(unittest.TestCase):
    def test_input_canvas_is_full_233_by_206(self):
        self.assertIn('main(0x1f,0x20,233,206)',PROFILE)
        self.assertIn('Rect c={0,0,232,205}; base.canvas=c',PROFILE)
        self.assertIn('base.glyph=c',PROFILE)
        self.assertIn('kPerspectiveHorizon = 70',PROFILE)
        self.assertNotIn('Rect g={20,74,200,151}',PROFILE)
        self.assertIn('renderManeuver(e,area,0.0)',KIT)

    def test_c04_measurement_is_separate_and_not_fabricated(self):
        self.assertIn('mainImageWidth(221),mainImageHeight(158)',PROFILE)
        for h in ('a','b'):
            self.assertIn('check(port,p_.main.'+h+',p_.mainImageWidth,p_.mainImageHeight)',RESOURCES)
        # No protocol/correlation change: only which profile fields supply expected dimensions.
        self.assertEqual(RESOURCES.replace('mainImageWidth','main.width').replace('mainImageHeight','main.height'),old('hud6wb_resources.hpp'))

    def test_no_6wa_or_old_main_limits_are_used_to_fit_6wb(self):
        self.assertIn('const Rect& useful=p_.base.glyph',(ROOT/'src/cluster/hud6wb_display.cpp').read_text())
        self.assertNotIn('k6wa',KIT)
        self.assertNotIn('199, 129',PROFILE)
        self.assertIn('p.main.width',CONFIG);self.assertIn('p.main.height',CONFIG)
        self.assertNotIn('221',CONFIG);self.assertNotIn('158',CONFIG)
        self.assertEqual(CONFIG,old('hud6wb_config.hpp').replace('p.base.streetChars,1,40','p.base.streetChars,1,kMaxLatinTextBytes'))

    def test_most_and_reference_data_are_unchanged(self):
        paths=['src/cluster/grafite_oro/artwork_data.inc','src/cluster/grafite_oro/reference.json',
               'src/most','src/dsi_client.cpp','src/nav_diag.hpp']
        self.assertEqual(subprocess.check_output(['git','diff',BASE,'--']+['milestone-7/'+p for p in paths],cwd=ROOT.parent),b'')

if __name__=='__main__':unittest.main()
