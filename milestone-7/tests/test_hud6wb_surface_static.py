"""Single central pair regression contracts. Source checks only, no builds."""
import pathlib
import re
import subprocess
import unittest
ROOT=pathlib.Path(__file__).resolve().parents[1]
BASE='5d03fe688230a9f34973a988eebddf82f76d29c4'
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

    def test_calibration_clock_and_interceptor_are_unchanged(self):
        for start,end in [('bool Hud6wbDisplay::interceptNavigation(', 'bool Hud6wbDisplay::mainChanged(')]:
            self.assertEqual(section(read('hud6wb_display.cpp'),start,end),section(original('hud6wb_display.cpp'),start,end))
        self.assertEqual(read('hud6wb_calibration.hpp'),original('hud6wb_calibration.hpp'))
        self.assertEqual(section(read('hud6wb_kit.cpp'),'KitBarState kitBarState(', 'sixwbkit::Maneuver::Value'),
                         section(original('hud6wb_kit.cpp'),'KitBarState kitBarState(', 'KitPlane kitPlane('))

    def test_geometry_colours_decoder_and_transport_unchanged(self):
        paths=['src/cluster/grafite_oro','src/cluster/nav_msg.hpp','src/cluster/hud6wb_codec.cpp','src/cluster/hud_terminal.cpp',
               'src/cluster/hud_terminal.hpp','src/most','src/dsi_client.cpp','src/aa_session.cpp']
        result=subprocess.check_output(['git','diff',BASE,'--']+['milestone-7/'+p for p in paths],cwd=ROOT.parent)
        self.assertEqual(result,b'')
        for name,value in [('COLOR_CONTOUR',2),('COLOR_INFILL',14),('COLOR_ACTIVE_SELECTION',3)]:
            self.assertRegex(read('hud6wb_kit.cpp'),r'static const uint8_t '+name+r'\s*=\s*'+str(value)+r';')

    def test_resources_query_only_existing_eight_handles(self):
        source=read('hud6wb_resources.hpp')
        query=section(source,'Batch queries() const {','// Queries only documented handles')
        self.assertEqual(len(re.findall(r'add\(b,',query)),8)
        self.assertIn('if(state_==kVerified)return true',source)
        self.assertNotIn('flat',source)
        for target in ['p_.base.frame','p_.main.a','p_.main.b','p_.icons.a','p_.icons.b','p_.lanes.a','p_.lanes.b','p_.bar']:
            self.assertIn('add(b,'+target+')',query)

if __name__=='__main__':unittest.main()
