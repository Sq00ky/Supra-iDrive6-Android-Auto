"""Source/data checks only: never builds or executes C++ or QNX tools."""
import ast
import json
import math
import pathlib
import re
import subprocess
import unittest
ROOT = pathlib.Path(__file__).resolve().parents[1]
KIT = ROOT / 'src/cluster/grafite_oro'
REF = json.loads((KIT / 'reference.json').read_text())
SOURCE = (KIT / 'artwork6wb.cpp').read_text()
ADAPTER = (ROOT / 'src/cluster/hud6wb_kit.cpp').read_text()
DISPLAY = (ROOT / 'src/cluster/hud6wb_display.cpp').read_text()

class KitStaticTests(unittest.TestCase):
    def test_all_reference_coordinates_and_styles_are_preserved(self):
        data = (KIT / 'artwork_data.inc').read_text()
        points = ast.literal_eval(data.split('points[] = ', 1)[1].split(';', 1)[0].replace('{','[').replace('}',']'))
        expected = [p for _,_,_,cs in REF['shapes'] for _,ps in cs for p in ps]
        self.assertEqual(points, expected)
        shapes = data.split('shapes[] = ', 1)[1].split(';', 1)[0]
        actual = re.findall(r'{Color::(\w+),Color::(\w+),([\d.]+),(\d+),(\d+)}', shapes)
        self.assertEqual(len(actual), len(REF['shapes']))
        for row, original in zip(actual, REF['shapes']):
            self.assertEqual(row[:2], tuple(original[:2]))
            self.assertEqual(float(row[2]), original[2])
            self.assertEqual(int(row[4]), len(original[3]))

    def test_explicit_mapping_and_no_example_selection(self):
        mapping = dict((int(n),name) for n,name in re.findall(r'case (\d+):return sixwbkit::Maneuver::(\w+);',ADAPTER))
        self.assertEqual(set(mapping), set(range(47)))
        for number,name in ((7,'Left'),(8,'Right'),(35,'RoundaboutCCWAngle'),(37,'Ferry'),(38,'TrainFerry'),(42,'DestinationRight'),(46,'RoundaboutExitCCW')):
            self.assertEqual(mapping[number],name)
        self.assertNotIn('exitExample', SOURCE)
        self.assertNotIn('variant(', SOURCE)
        self.assertIn('sign=cw?1:-1', SOURCE)
        self.assertIn('exit=180+sign*e.exitAngle', SOURCE)
        self.assertIn('e.hasExitNumber', SOURCE)

    def test_angle_convention_and_uniform_fit_data(self):
        # Independent mathematical checks, NOT execution of the C++ implementation.
        for angle in (1,15,90,127,180,225,270,359,360):
            ccw=math.radians(180-angle);cw=math.radians(180+angle)
            self.assertAlmostEqual(math.sin(cw),-math.sin(ccw))
            self.assertAlmostEqual(math.cos(cw),math.cos(ccw))
        for fill,stroke,width,contours in REF['shapes']:
            points=[p for _,ps in contours for p in ps]
            if not points:continue
            r=width/2
            left=min(x-r for x,y in points);right=max(x+r for x,y in points)
            top=min(y-r for x,y in points);bottom=max(y+r for x,y in points)
            if left>=right or top>=bottom:continue
            for w,h in ((180,77),(220,157),(232,205),(282,39)):
                for margin in (0,2):
                    scale=min((w-2*margin)/(right-left),(h-2*margin)/(bottom-top))
                    dx=(w-(left+right)*scale)/2;dy=(h-(top+bottom)*scale)/2
                    for x,y in points:
                        self.assertGreaterEqual(x*scale+dx-r*scale,-1e-6)
                        self.assertLessEqual(x*scale+dx+r*scale,w+1e-6)
                        self.assertGreaterEqual(y*scale+dy-r*scale,-1e-6)
                        self.assertLessEqual(y*scale+dy+r*scale,h+1e-6)

    def test_lanes_keep_every_direction_and_highlight(self):
        body=ADAPTER.split('bool GrafiteKit::laneEvent(',1)[1].split('bool GrafiteKit::encode(',1)[0]
        self.assertIn('for(size_t j=0;j<v.lanes[i].size();++j)',body)
        self.assertIn('Direction(d.shape,d.highlighted)',body)
        self.assertNotIn('mask',body)
        self.assertIn('laneTip(d.shape,d.highlighted)',SOURCE)
        self.assertIn('-45,45,-90,90,-135,135,180,180',SOURCE)
        self.assertIn('area.y-top*scale',SOURCE)

    def test_single_pair_and_preserved_bar(self):
        self.assertIn('uint16_t target=other(mainVisible_,p_.main)',DISPLAY)
        self.assertIn('const Rect& useful=p_.base.glyph',DISPLAY)
        self.assertIn('kit_.central(v,target,useful,drawing)',DISPLAY)
        self.assertIn('setVisibility(p_.bar,bar.visible)',DISPLAY)
        for obsolete in ('kitPlane(', 'p_.flat', 'flat plane is not configured', 'bool perspective'):
            self.assertNotIn(obsolete,DISPLAY)
        self.assertIn('if(h!=0x1f && h!=0x20)return false',ADAPTER)
        self.assertNotIn('bmwManeuver',DISPLAY)
        self.assertNotIn('shapeFor(',DISPLAY)

    def test_build_lists_and_cpp98_subset(self):
        files=('src/cluster/hud6wb_kit.cpp','src/cluster/grafite_oro/artwork6wb.cpp','src/cluster/grafite_oro/hudterminal_palette.cpp')
        for build in ('Makefile','CMakeLists.txt','tests/Makefile'):
            text=(ROOT/build).read_text()
            for name in files:self.assertIn(name,text)
        for file in list(KIT.glob('*.hpp'))+list(KIT.glob('*.cpp'))+[ROOT/'src/cluster/hud6wb_kit.cpp']:
            text=file.read_text()
            for unsupported in (r'\benum class\b',r'\bauto\b',r'\bnullptr\b',r'\bstd::move\b',r'\bu8"',r'\bstatic_assert\b'):
                self.assertIsNone(re.search(unsupported,text),file.name)

    def test_encoding_bounds_and_atomic_failure(self):
        self.assertIn('p.size()>99',ADAPTER)
        self.assertIn('if(!stroke(b,h,s.contours[c],s.strokeWidth,bounds,item))return false',ADAPTER)
        self.assertNotIn('b.add(kSetLineColor',ADAPTER)
        self.assertNotIn('b.add(kSetLineWidth',ADAPTER)
        self.assertIn('kPolygon',ADAPTER)
        self.assertIn('if(xs.size()%2)return false',ADAPTER)
        self.assertIn('out.calls.insert(out.calls.end(),b.calls.begin(),b.calls.end())',ADAPTER)
        self.assertIn('queuedCall_+128',DISPLAY)
        self.assertIn('Batch chunk("6wb-kit-chunk",false)',DISPLAY)
        self.assertNotIn('renderFrame(v,false)',DISPLAY)

    def test_balanced_delimiters_without_compilation(self):
        pattern=r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\''
        for path in list(KIT.glob('*.cpp'))+list(KIT.glob('*.hpp'))+[ROOT/'src/cluster/hud6wb_kit.cpp',ROOT/'src/cluster/hud6wb_display.cpp']:
            stack=[]
            for char in re.sub(pattern,'',path.read_text(),flags=re.S):
                if char in '([{':stack.append(char)
                elif char in ')]}':self.assertEqual(stack.pop(),{')':'(',']':'[','}':'{'}[char],path.name)
            self.assertFalse(stack,path.name)

if __name__=='__main__':unittest.main()
