"""Source contracts and independent Python geometry/codec models. NEVER compiles C++."""
import ast
import math
import pathlib
import re
import unittest
ROOT=pathlib.Path(__file__).resolve().parents[1]
DISPLAY=(ROOT/'src/cluster/hud6wb_display.cpp').read_text()
CLOCK=(ROOT/'src/cluster/hud6wb_calibration.hpp').read_text()
CODEC=(ROOT/'src/cluster/hud6wb_codec.cpp').read_text()
ART=(ROOT/'src/cluster/grafite_oro/artwork6wb.cpp').read_text()
ADAPTER=(ROOT/'src/cluster/hud6wb_kit.cpp').read_text()

def steps(side):
    result=[]
    for i in range(41):
        a=5*min(i,40-i);j=min(i+1,40);z=5*min(j,40-j);top=5*i;bottom=top+5
        if a==z:p=[(a,top),(a+2,top),(a+2,bottom),(a,bottom)]
        elif z>a:p=[(a,top),(a+2,top),(a+2,bottom-2),(z+2,bottom-2),(z+2,bottom),(a,bottom)]
        else:p=[(a,top),(a+2,top),(a+2,bottom),(z,bottom),(z,bottom-2),(a,bottom-2)]
        q=[(232-x if side else x,y)for x,y in p]
        result.append(q[::-1]if side else q)
    return result

def picture(test):
    # Read the production glyph DATA; independent pixel/RLE model below.
    table=CODEC.split('glyphs[][7]={',1)[1].split('};',1)[0]
    glyphs=ast.literal_eval('['+table.replace('{','[').replace('}',']')+']')
    alphabet=re.search(r'alphabet\[\]="([^"]+)"',CODEC)[1]
    background=0xffff0000 if test else 0xffffff00;ink=0xff00ffff if test else 0xff00ff00
    px=[background]*(305*44)
    for y in range(44):
        for x in range(305):
            if x in (0,304)or y in(0,43)or((3<=x<=7 or 297<=x<=301)and(3<=y<=7 or 36<=y<=40)):px[y*305+x]=ink
    text='BITMAP TEST '+str(test+1);scale=2;x0=(305-(len(text)*6-1)*scale)//2;y0=(44-7*scale)//2
    for k,ch in enumerate(text):
        for y,row in enumerate(glyphs[alphabet.index(ch)]):
            for x in range(5):
                if row&(1<<(4-x)):
                    for yy in range(scale):
                        for xx in range(scale):px[(y0+y*scale+yy)*305+x0+k*6*scale+x*scale+xx]=ink
    return px

def rle(px):
    def pixel(c):return bytes((c>>24&255,c&255,c>>8&255,c>>16&255))
    out=bytearray();i=0
    while i<len(px):
        n=1
        while i+n<len(px)and n<127 and px[i+n]==px[i]:n+=1
        if n>=2:out.append(128|n);out.extend(pixel(px[i]));i+=n;continue
        start=i;i+=1
        while i<len(px)and i-start<127:
            if i+1<len(px)and px[i]==px[i+1]:break
            i+=1
        out.append(i-start)
        for c in px[start:i]:out.extend(pixel(c))
    out.extend(b'\0'*(8-len(out)%8));return bytes(out)

def unrle(data,count):
    out=[];at=0
    def pixel(raw):return(raw[0]<<24)|(raw[3]<<16)|(raw[2]<<8)|raw[1]
    while len(out)<count:
        tag=data[at];at+=1;n=tag&127
        if not n:raise AssertionError('zero run before final padding')
        if tag&128:out.extend([pixel(data[at:at+4])]*n);at+=4
        else:
            for _ in range(n):out.append(pixel(data[at:at+4]));at+=4
    return out

class DualChecks(unittest.TestCase):
    def test_steps_source_matches_documented_model(self):
        for expression in ('i<41','std::min(i,40-i)','top=5*(int)i,bottom=top+5','side?232-xy[k][0]:xy[k][0]',
                           'side?5:6','side && i%5==0','left=232-a-6,right=232-a-3'):
            self.assertIn(expression,DISPLAY)
        body=DISPLAY.split('Batch Hud6wbDisplay::geometryCalibrationBatch()',1)[1].split('bool Hud6wbDisplay::interceptNavigation',1)[0]
        self.assertNotIn('kSetLineColor',body);self.assertNotIn('Horizon',body)

    def test_all_steps_bounds_and_endpoints(self):
        for side in (0,1):
            series=steps(side);self.assertEqual(len(series),41)
            self.assertEqual(min(y for p in series for x,y in p),0)
            self.assertEqual(max(y for p in series for x,y in p),205)
            for i,p in enumerate(series):
                self.assertTrue(4<=len(p)<=6)
                self.assertTrue(all(0<=x<=232 and 5*i<=y<=5*i+5 for x,y in p))
                area=sum(p[j][0]*p[(j+1)%len(p)][1]-p[(j+1)%len(p)][0]*p[j][1]for j in range(len(p)))
                self.assertGreater(area,0)
        self.assertEqual(min(x for p in steps(0)for x,y in p),0)
        self.assertEqual(max(x for p in steps(1)for x,y in p),232)

    def test_steps_separate_sides_and_detached_ticks(self):
        self.assertLess(max(x for p in steps(0)for x,y in p),min(x for p in steps(1)for x,y in p))
        ticks=[]
        for i in range(0,41,5):
            a=5*min(i,40-i);box=(232-a-6,5*i+1,232-a-3,5*i+3);ticks.append(box)
            self.assertTrue(0<=box[0]<box[2]<=232 and 0<=box[1]<box[3]<=205)
            self.assertLess(box[2],232-a-2)
        self.assertEqual(len(ticks),9)

    def test_batch_budgets_and_bitmaps(self):
        self.assertEqual(1+1+10+40+6+2+4,64)
        self.assertEqual(1+2+82+9+6+2+4,106)
        self.assertLessEqual(106,128)
        self.assertIn('if(b.calls.size()>128)',DISPLAY)
        self.assertIn('queuedCall_+128',DISPLAY)
        self.assertIn('Call c(0xC05,payload);c.txType=0',DISPLAY)

    def test_two_ten_second_holds_and_no_nav_interleaving(self):
        self.assertIn('deadline_=now+PALETTE_CALIBRATION_SECONDS',CLOCK)
        self.assertIn('PALETTE_CALIBRATION_SECONDS = 10.0',CLOCK)
        self.assertIn('test_=1;phase_=Pending;deadline_=0',CLOCK)
        self.assertLess(DISPLAY.index('if(calibration_.due(now) && calibration_.test()==0)'),DISPLAY.index('Batch b=calibration_.test()==0?'))
        self.assertIn('Holding && !calibration_.due(now))return true',DISPLAY)
        self.assertIn('calibration_.dispatched(now)',DISPLAY)
        self.assertIn('handles[]={p_.main.a,p_.main.b,p_.icons.a,p_.icons.b}',DISPLAY)
        self.assertIn('fresh_=true;dirty_=true;sent_=false',DISPLAY)
        self.assertNotIn('sleep(10)',DISPLAY)

    def test_bitmap_dimensions_text_colours_and_corners(self):
        self.assertIn('const unsigned w=305,h=44,scale=2',CODEC)
        self.assertIn('"BITMAP TEST 2":"BITMAP TEST 1"',CODEC)
        for test in range(2):
            px=picture(test);self.assertEqual(len(px),305*44)
            self.assertEqual(set(px),{0xffff0000,0xff00ffff}if test else{0xffffff00,0xff00ff00})
            for at in (0,304,305*43,305*44-1):self.assertEqual(px[at],px[0])
            self.assertNotEqual(px[306],px[0])
        self.assertNotEqual(picture(0),picture(1))

    def test_bitmap_rle_roundtrip_padding_and_cap(self):
        for test in range(2):
            px=picture(test);data=rle(px)
            self.assertEqual(unrle(data,len(px)),px)
            self.assertEqual(len(data)%8,0)
            self.assertLessEqual(len(data)+10,4096)
        self.assertIn('r.size()+10>cap',CODEC)
        self.assertIn('bitmap(target,0,calibrationPicture(test),305,44,p_.maxBitmapBytes)',DISPLAY)

    def test_no_native_218_and_no_thick_line_guidance(self):
        encode=ADAPTER.split('bool GrafiteKit::encode(',1)[1].split('bool GrafiteKit::central(',1)[0]
        self.assertNotIn('kSetLineColor',encode);self.assertNotIn('kSetLineWidth',encode)
        self.assertIn('setFillColor(h,color)',encode)
        self.assertIn('stroke(b,h,s.contours[c],s.strokeWidth,bounds,item)',encode)
        self.assertIn('return contour(silhouette(path,half,head),true,Color::White,Color::Red,6)',ART)
        self.assertIn('scene.push_back(guidance(arrow,uturn?45:60))',ART)

    def test_uniform_fit_and_circular_roundabout(self):
        self.assertIn('scale=std::min((area.width-2*margin)/(r-l),(area.height-2*margin)/(b-t))',ART)
        self.assertIn('p.x=p.x*scale+x;p.y=p.y*scale+y',ART)
        self.assertIn('arc(outer.points,0,352.5,360);arc(inner.points,0,352.5,190)',ART)
        for radius in(190,360):
            scale=min(232/(2*radius),205/(2*radius))
            for angle in range(0,360,5):
                x=radius*math.sin(math.radians(angle))*scale;y=-radius*math.cos(math.radians(angle))*scale
                self.assertAlmostEqual(math.hypot(x,y),radius*scale)

    def test_palette_and_separate_symbol_families(self):
        for role,index in [('ROAD_FILL',3),('ROAD_EDGE',2),('GUIDANCE_FILL',6),('GUIDANCE_EDGE',5)]:
            self.assertIn('COLOR_'+role+' = '+str(index),ADAPTER)
        self.assertIn('scene=flatRoad(e.maneuver)',ART)
        self.assertIn('if(round)scene=roundabout(e)',ART)
        self.assertIn('symbolStyle(scene)',ART)
        self.assertIn('case Maneuver::UTurnLeft',ART)
        self.assertIn('case Maneuver::MergeRight',ART)
        mapping=re.findall(r'case (\d+):return sixwbkit::Maneuver::',ADAPTER)
        self.assertEqual(set(map(int,mapping)),set(range(47)))

if __name__=='__main__':unittest.main()
