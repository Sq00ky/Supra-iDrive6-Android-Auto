#!/usr/bin/env python3
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
"""Golden accepted contours read back from production MOST commands, not the design generator."""
import hashlib
import json
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
                        body=ink[0]; a=body.index((53,41)); b=body.index((79,45))
                        arc=body[a:b+1] if a<b else body[a:]+body[:b+1]
                        self.assertEqual(len(arc)-1,12)
                        self.assertTrue(all(cross(arc[i-1],arc[i],arc[i+1])>0 for i in range(1,len(arc)-1)))

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


if __name__=='__main__': unittest.main()
