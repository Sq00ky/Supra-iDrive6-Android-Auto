"""Single central pair regression contracts. Source checks only, no builds."""
import pathlib
import re
import subprocess
import unittest
ROOT=pathlib.Path(__file__).resolve().parents[1]
BASE='473b393e2ad6894d54b8b284bfa07c8cdc7ec52b'
FILES=['hud6wb_display.cpp','hud6wb_display.hpp','hud6wb_kit.cpp','hud6wb_kit.hpp','hud6wb_profile.hpp','hud6wb_resources.hpp']
def read(name):return (ROOT/'src/cluster'/name).read_text()
def original(name):return subprocess.check_output(['git','show',BASE+':milestone-7/src/cluster/'+name],cwd=ROOT.parent,text=True)
def section(text,start,end):return text.split(start,1)[1].split(end,1)[0]

class SingleSurfaceChecks(unittest.TestCase):
    def test_no_functional_flat_or_palette_file_dependency(self):
        for name in FILES:
            source=read(name)
            for term in ('flat.a','flat.b','p_.flat','p.flat','flatGlyph','flat_','kitPlane','configureKitPlanes','6wb-kit.palette','fopen('):
                self.assertNotIn(term,source,name)
        self.assertNotIn('configureKitPlanes',(ROOT/'src/cluster/nav_core.cpp').read_text())
        self.assertIn('main_(provisionalKitPalette()),lanes_(main_)',read('hud6wb_kit.cpp'))

    def test_all_families_have_one_target_and_one_coordinate_rectangle(self):
        body=section(read('hud6wb_display.cpp'),'Batch Hud6wbDisplay::render(', 'void Hud6wbDisplay::remember(')
        self.assertIn('target=other(mainVisible_,p_.main)',body)
        self.assertIn('useful=p_.base.glyph',body)
        self.assertIn('kit_.central(v,target,useful,drawing)',body)
        self.assertIn('switchVisibility(mainVisible_,target)',body)
        self.assertNotIn('PlanePolicy::FlatOnly',body)
        self.assertNotIn('bar.visible)==',body)
        self.assertIn('if(h!=0x1f && h!=0x20)return false',read('hud6wb_kit.cpp'))
        self.assertIn('p.main.a!=0x1f || p.main.b!=0x20',read('hud6wb_profile.hpp'))

    def test_most_and_bitmap_wire_codec_are_unchanged(self):
        paths=['src/most','src/dsi_client.cpp']
        result=subprocess.check_output(['git','diff',BASE,'--']+['milestone-7/'+p for p in paths],cwd=ROOT.parent)
        self.assertEqual(result,b'')
        now=read('hud6wb_codec.cpp');old=original('hud6wb_codec.cpp')
        self.assertEqual(section(now,'static void pixel(', '} }'),
                         section(old,'static void pixel(', 'RoadShield roadShield('))

    def test_known_resources_have_no_active_probe_or_reply_dependency(self):
        source=read('hud6wb_resources.hpp')
        active=source.split('// ORIGINAL C04 DISCOVERY',1)[0]
        self.assertIn('return Batch("6wb-known-surfaces")',active)
        self.assertIn('port.status()==kReady',active)
        for obsolete in ('imageSize(', 'getSize(', 'waitIdle(', 'port.submit(', 'kQuerying;'):
            self.assertNotIn(obsolete,active)
        self.assertIn('//     Batch queries() const {',source)

if __name__=='__main__':unittest.main()
