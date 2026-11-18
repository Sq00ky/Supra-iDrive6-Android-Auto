"""Work package integrity and layout contracts; no renderer reimplementation."""
import pathlib
import unittest
from work_renderer_integrity import assert_work_sources
ROOT = pathlib.Path(__file__).resolve().parents[1]

class WorkRendererChecks(unittest.TestCase):
    def test_destination_update_reaches_central_renderer_only(self):
        source=(ROOT/'src/cluster/grafite_oro/artwork6wb.cpp').read_text(encoding='utf-8')
        adapter=(ROOT/'src/cluster/hud6wb_kit.cpp').read_text(encoding='utf-8')
        self.assertIn('case 39:return sixwbkit::Maneuver::Destination;',adapter)
        self.assertIn('renderManeuver(e,area,0.0)',adapter)
        self.assertIn('else if(!miniature&&e.maneuver>=Maneuver::Destination&&e.maneuver<=Maneuver::DestinationRight)',source)
        self.assertIn('scene=destinationArtwork(e.maneuver)',source)
        self.assertIn('panel(111,0,220,157,Color::Gray)',source)
        self.assertIn('p.graphite=COLOR_ROAD_FILL;p.black=9;',adapter)

    def test_authoritative_sources_match_import_manifest(self):
        assert_work_sources(self, ROOT)

    def test_work_scaling_and_miniature_are_distinct(self):
        source = (ROOT/'src/cluster/grafite_oro/artwork6wb.cpp').read_text(encoding='utf-8')
        self.assertIn('if(round&&!miniature)', source)
        self.assertIn('const double scale=0.96', source)
        self.assertIn('fit(scene,area,margin,miniature)', source)
        self.assertIn('maneuverArtwork(e,area,0,true)', source)
        self.assertNotIn('0.85', source)

    def test_old_graphic_engine_is_removed(self):
        for name in ('hud6wb_artwork.cpp','hud6wb_artwork.hpp'):
            self.assertFalse((ROOT/'src/cluster'/name).exists())
        for name in ('Makefile','CMakeLists.txt','tests/Makefile','tools/preview_6wb.py'):
            self.assertNotIn('hud6wb_artwork', (ROOT/name).read_text(encoding='utf-8'))
        codec = (ROOT/'src/cluster/hud6wb_codec.cpp').read_text(encoding='utf-8')
        self.assertNotIn('RoadShield', codec)
        self.assertNotIn('shieldPicture', codec)

if __name__ == '__main__':
    unittest.main()
