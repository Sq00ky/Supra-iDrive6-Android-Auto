#!/usr/bin/env python3
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
"""Golden accepted contours read back from production MOST commands, not the design generator."""
import hashlib
import json
import math
from pathlib import Path
import subprocess
import sys
import unittest
import xml.etree.ElementTree as ET

BINARY = str(Path(sys.argv.pop(1)).resolve())
NS = {'s': 'http://www.w3.org/2000/svg'}
APPROVED = json.loads((Path(__file__).parent/'fixtures/hud-unified-approved.json').read_text())


def render(stage, mode, *args):
    root = ET.fromstring(subprocess.check_output([BINARY,str(stage),str(mode),*args],text=True))
    return root.findall('.//s:g[@data-type]',NS) if not args else root.findall('.//s:g[@data-shape]',NS)


def points(poly):
    return [tuple(map(int,p.split(','))) for p in poly.get('points').split()]


def digest(p):
    variants = [p[i:]+p[:i] for i in range(len(p))]
    p = p[::-1]
    variants += [p[i:]+p[:i] for i in range(len(p))]
    return hashlib.sha256(json.dumps(min(variants),separators=(',',':')).encode()).hexdigest()


def cross(a,b,c):
    return (b[0]-a[0])*(c[1]-b[1])-(b[1]-a[1])*(c[0]-b[0])


def on(a,b,c):
    return cross(a,b,c)==0 and min(a[0],b[0])<=c[0]<=max(a[0],b[0]) and min(a[1],b[1])<=c[1]<=max(a[1],b[1])


def touches(a,b,c,d):
    return (cross(a,b,c)*cross(a,b,d)<0 and cross(c,d,a)*cross(c,d,b)<0) or any((on(a,b,c),on(a,b,d),on(c,d,a),on(c,d,b)))


def simple(p):
    assert 3<=len(p)<=99 and len(set(p))==len(p)
    for i,a in enumerate(p):
        b=p[(i+1)%len(p)]
        for j in range(i+2,len(p)):
            if i==0 and j==len(p)-1: continue
            assert not touches(a,b,p[j],p[(j+1)%len(p)]),p


def chain(p, start, end):
    first=p.index(start); p=p[first:]+p[:first]
    return p[:p.index(end)+1]


def point_segment_distance(p,a,b):
    dx,dy=b[0]-a[0],b[1]-a[1]
    t=max(0,min(1,((p[0]-a[0])*dx+(p[1]-a[1])*dy)/(dx*dx+dy*dy)))
    return math.hypot(p[0]-a[0]-t*dx,p[1]-a[1]-t*dy)


def sharp_elbow_width(body):
    inside=chain(body,(69,44),(63,45))
    tail=next(p for p in body if p[0]==79 and p[1]>=55)
    outside=chain(body,(43,51),tail)
    # For disjoint line segments the shortest distance has an endpoint on at least one segment.
    return min(min(point_segment_distance(a,c,d),point_segment_distance(b,c,d),
                   point_segment_distance(c,a,b),point_segment_distance(d,a,b))
               for a,b in zip(inside,inside[1:]) for c,d in zip(outside,outside[1:]))


class UnifiedTest(unittest.TestCase):
    def test_approved_native_contours_and_all_stages(self):
        baseline = {}
        for mode in range(2):
            for stage in range(5):
                for cell in render(stage,mode)[:61]:
                    t=int(cell.get('data-type'))
                    if t in (30,31,32,33,34,35,43,44,45,46): continue
                    polys=cell.findall('.//s:polygon',NS)
                    ink=[points(p) for p in polys if p.get('fill')=='#ffe4ab']
                    for p in polys: simple(points(p))
                    wire=[(p.get('fill'),points(p))for p in polys]
                    if mode: self.assertEqual(wire,baseline[t,stage])
                    else: baseline[t,stage]=wire
                    key=f'{t}-'+('near' if stage else 'far')
                    if key not in APPROVED['main']: continue
                    golden=APPROVED['main'][key]
                    self.assertEqual(digest(ink[-1]),golden.get('head',golden['body']))
                    if stage in (0,4): self.assertEqual(digest(ink[0]),golden['body'])
                    if t==39:
                        self.assertEqual(len(polys),2)
                        hole=points(polys[-1]); self.assertEqual(digest(hole),golden['hole'])
                        self.assertEqual(len(hole),16)
                        turns=[cross(hole[i-1],hole[i],hole[(i+1)%16])for i in range(16)]
                        self.assertTrue(all(x>0 for x in turns) or all(x<0 for x in turns))
                    if t==9:
                        body=ink[0]
                        self.assertEqual(chain(body,(69,44),(63,45)),
                                         [(69,44),(68,42),(67,42),(65,43),(63,45)])
                        self.assertGreaterEqual(sharp_elbow_width(body),9.8)
                        tail=next(p for p in body if p[0]==79 and p[1]>=55)
                        outer=chain(body,(43,51),tail)
                        self.assertTrue(all(cross(outer[i-1],outer[i],outer[i+1])>0
                                            for i in range(1,len(outer)-1)))

    def test_approved_lane_shapes_both_colours(self):
        baseline={}
        for mode in range(2):
            cells=render(0,mode,'lanes'); self.assertEqual(len(cells),18)
            for cell in cells:
                shape=cell.get('data-shape'); selected=int(cell.get('data-highlight'))
                self.assertEqual(cell.find('s:svg',NS).get('viewBox'),'0 0 184 17')
                polys=cell.findall('.//s:polygon',NS); self.assertEqual(len(polys),2)
                for role,poly in zip(('body','head'),polys):
                    p=points(poly); simple(p)
                    self.assertEqual(digest(p),APPROVED['lanes'][shape][role])
                    self.assertEqual(poly.get('fill'),'#ffe4ab'if selected else'#777')
                    self.assertTrue(all(0<=x<=183 and 0<=y<=16 for x,y in p))
                wire=[ET.tostring(p)for p in polys]
                if mode:self.assertEqual(wire,baseline[shape,selected])
                else:baseline[shape,selected]=wire

    def test_lane_width_symmetry_and_outside_cuts(self):
        for mode in range(2):
            cells=render(0,mode,'lanes')
            for selected in range(2):
                drawings={int(c.get('data-shape')):[points(p) for p in c.findall('.//s:polygon',NS)]
                          for c in cells if int(c.get('data-highlight'))==selected}
                for shape in (4,5,6,7):
                    body,head=drawings[shape]
                    foot=[x for x,y in body if y==15]
                    self.assertEqual(len(foot),2)
                    self.assertEqual(abs(foot[0]-foot[1]),2)
                for left,right in ((4,5),(6,7)):
                    for a,b in zip(drawings[left],drawings[right]):
                        self.assertEqual({(182-x,y)for x,y in a},set(b))
                for shape in (4,5):
                    head=drawings[shape][1]
                    self.assertEqual({(x,14-y)for x,y in head},set(head))
                head=drawings[6][1]
                self.assertEqual({(99-y,99-x)for x,y in head},set(head))
                body=drawings[6][0]
                # Two flat diagonal edges: x+y=100 and97 (3/sqrt(2), about2.12 native px).
                self.assertTrue({(92,8),(87,13),(86,11),(92,5)}<=set(body))
                self.assertIn((95,7),body)
                self.assertNotIn((95,6),body)
                self.assertIn((95,7),drawings[4][0])
                self.assertNotIn((95,6),drawings[4][0])


if __name__=='__main__': unittest.main()
